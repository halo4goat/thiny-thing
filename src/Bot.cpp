#include "Bot.h"

#include <windows.h>
#include <timeapi.h>

#include <algorithm>

bool Bot::start(Mode m, InputKind kind, std::string& err) {
    stop();
    if (m == Mode::Idle) return true;
    if (!proc_.attached()) { err = "attach to the game first"; return false; }
    std::vector<MacroAction> actions;
    if (m == Mode::Playing) {
        actions = macro().actions;
        if (actions.empty()) { err = "no macro loaded"; return false; }
        clicksSent = 0;
        attempts = 0;
        isolatedSent = 0;
        isolatedSkipped = 0;
        maxLoopGapUs = 0;
        lastFiredIdx = -1;
    } else {
        clicksRecorded = 0;
    }
    float probe = 0;
    if (auto r = proc_.readPlayerX(probe); r != ReadResult::Ok) {
        err = std::string("cannot read player X: ") + describe(r) + " (be inside a level)";
        return false;
    }
    mode_ = m;
    worker_ = std::jthread([this, m, kind, a = std::move(actions)](std::stop_token st) mutable {
        run(st, m, kind, std::move(a));
    });
    return true;
}

void Bot::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

std::string Bot::status() const { std::lock_guard lk(mu_); return status_; }
void Bot::setStatus(std::string s) { std::lock_guard lk(mu_); status_ = std::move(s); }
Macro Bot::macro() const { std::lock_guard lk(mu_); return macro_; }
void Bot::setMacro(Macro m) { std::lock_guard lk(mu_); macro_ = std::move(m); }

void Bot::run(std::stop_token st, Mode mode, InputKind kind, std::vector<MacroAction> actions) {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const int vk = virtualKeyFor(kind);

    const bool soundOn = mode == Mode::Playing && clickSound.load();
    bool held = false;
    auto syncHold = [&](bool want) {
        if (want && !held) {
            if (!sendClick(kind, true)) setStatus("SendInput failed (elevation mismatch?)");
            held = true;
            ++clicksSent;
            if (soundOn) sounder_.press();
        } else if (!want && held) {
            sendClick(kind, false);
            held = false;
            if (soundOn) sounder_.release();
        }
    };

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    auto nowTicks = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; };

    const bool useIsolated =
        mode == Mode::Playing && kind != InputKind::Mouse && isolatedMouse.load();
    bool mouseHeld = false;
    long long mouseUpAt = 0;

    auto isolatedDown = [&] {
        if (mouseHeld || !proc_.cursorOverGame()) { ++isolatedSkipped; return; }
        if (sendClick(InputKind::Mouse, true)) {
            mouseHeld = true;
            mouseUpAt = nowTicks() + static_cast<long long>(
                static_cast<double>(isolatedHoldMs.load()) * static_cast<double>(freq.QuadPart) / 1000.0);
            ++isolatedSent;
        }
    };
    auto isolatedUp = [&](bool force) {
        if (mouseHeld && (force || nowTicks() >= mouseUpAt)) {
            sendClick(InputKind::Mouse, false);
            mouseHeld = false;
        }
    };

    float prevX = 0;
    bool havePrev = false;
    std::size_t idx = 0;
    bool needSeek = true;
    bool keyPrev = false, keyPrevValid = false;
    long long prevTick = 0;
    bool havePrevTick = false;

    while (!st.stop_requested()) {
        float x = 0;
        const ReadResult r = proc_.readPlayerX(x);
        if (r == ReadResult::ProcessGone) { setStatus("game process exited"); break; }
        if (r != ReadResult::Ok) {
            if (mode == Mode::Playing) {
                syncHold(false);
                if (useIsolated) isolatedUp(true);
            }
            setStatus(std::string("waiting: ") + describe(r));
            havePrev = false; needSeek = true; keyPrevValid = false; havePrevTick = false;
            Sleep(5);
            continue;
        }
        lastX = x;
        {
            const long long t = nowTicks();
            if (havePrevTick && mode == Mode::Playing) {
                const auto gap = static_cast<std::uint32_t>((t - prevTick) * 1000000 / freq.QuadPart);
                if (gap > maxLoopGapUs.load()) maxLoopGapUs = gap;
            }
            prevTick = t;
            havePrevTick = true;
        }
        const bool fg = proc_.isForeground();
        const bool reset = havePrev && x < prevX - resetThreshold.load();
        if (reset) ++attempts;

        if (mode == Mode::Playing) {
            if (useIsolated) isolatedUp(false);
            if (!fg) {
                syncHold(false);
                if (useIsolated) isolatedUp(true);
                needSeek = true;
                setStatus("paused: game window not focused");
            } else {
                if (reset) needSeek = true;
                if (needSeek) {
                    idx = seekIndex(actions, x);
                    syncHold(holdBefore(actions, idx));
                    needSeek = false;
                }
                const float fire = x + leadUnits.load();
                while (idx < actions.size() && actions[idx].x <= fire) {
                    const bool press = actions[idx].down && !held;
                    syncHold(actions[idx].down);
                    lastFiredIdx = static_cast<int>(idx);
                    lastFiredMacroX = actions[idx].x;
                    lastFiredActualX = x;
                    if (press && useIsolated) isolatedDown();
                    ++idx;
                }
                setStatus("playing");
            }
        } else {  // Recording
            const bool down = fg && (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (reset) {
                auto it = std::lower_bound(actions.begin(), actions.end(), x,
                    [](const MacroAction& a, float v) { return a.x < v; });
                actions.erase(it, actions.end());
                const bool trailingDown = !actions.empty() && actions.back().down;
                if (down != trailingDown) actions.push_back({x, down});
                clicksRecorded = static_cast<std::uint64_t>(
                    std::count_if(actions.begin(), actions.end(), [](const MacroAction& a) { return a.down; }));
                keyPrev = down; keyPrevValid = true;
            }
            if (!fg) {
                keyPrevValid = false;
                setStatus("recording paused: game window not focused");
            } else {
                if (!keyPrevValid) { keyPrev = down; keyPrevValid = true; }
                else if (down != keyPrev) {
                    if (!(actions.empty() && !down)) {  // ignore a release with no matching press
                        actions.push_back({x, down});
                        if (down) ++clicksRecorded;
                    }
                    keyPrev = down;
                }
                setStatus("recording");
            }
        }

        prevX = x;
        havePrev = true;
        if (mode == Mode::Playing) std::this_thread::yield(); else Sleep(1);
    }

    if (mode == Mode::Playing) {
        syncHold(false);
        if (useIsolated) isolatedUp(true);
    } else {
        if (!actions.empty() && actions.back().down) actions.push_back({lastX.load(), false});
        setMacro(Macro{std::move(actions)});
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    timeEndPeriod(1);
    mode_ = Mode::Idle;
    if (!st.stop_requested()) return;  // keep the exit reason in status
    setStatus("idle");
}
