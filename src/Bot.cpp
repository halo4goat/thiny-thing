#include "Bot.h"

#include <windows.h>
#include <timeapi.h>

#include <algorithm>

bool Bot::start(Mode m, InputKind kind, std::string& err) {
    stop();
    if (m == Mode::Idle) return true;
    if (!proc_.attached()) { err = "attach to the game first"; return false; }
    std::vector<MacroAction> actions;
    RefTrack ref;
    const bool useYFix = m == Mode::Playing && yFix.load();
    if (m == Mode::Playing) {
        actions = macro().actions;
        if (actions.empty()) { err = "no macro loaded"; return false; }
        if (useYFix) {
            { std::lock_guard lk(mu_); ref = ref_; }
            if (ref.empty()) { err = "Y correction is on but no .ref file is loaded (it must sit next to the macro)"; return false; }
            if (!proc_.canWrite()) { err = "no write access to the game process (run both at the same elevation)"; return false; }
        }
        yFixWritten = 0;
        yFixFailed = 0;
        clicksSent = 0;
        attempts = 0;
        isolatedSent = 0;
        isolatedSkipped = 0;
        maxLoopGapUs = 0;
        lastFiredIdx = -1;
        estSpeed = 0.f;
        traceHead_ = 0;
        staleSkipped = 0;
        { std::lock_guard lk(mu_); deaths_.clear(); }
    } else {
        clicksRecorded = 0;
    }
    float probe = 0;
    if (auto r = proc_.readPlayerX(probe); r != ReadResult::Ok) {
        err = std::string("cannot read player X: ") + describe(r) + " (be inside a level)";
        return false;
    }
    mode_ = m;
    worker_ = std::jthread([this, m, kind, a = std::move(actions), rf = std::move(ref), useYFix](std::stop_token st) mutable {
        run(st, m, kind, std::move(a), std::move(rf), useYFix);
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
void Bot::setRef(RefTrack r) { std::lock_guard lk(mu_); ref_ = std::move(r); }
std::size_t Bot::refSize() const { std::lock_guard lk(mu_); return ref_.x.size(); }
std::vector<Bot::DeathRecord> Bot::deaths() const { std::lock_guard lk(mu_); return deaths_; }

void Bot::tracePush(double tMs, float x, float y, int idx, int event) {
    const std::uint32_t h = traceHead_.load(std::memory_order_relaxed);
    trace_[h % kTraceSize] = TraceRow{tMs, x, y, idx, event};
    traceHead_.store(h + 1, std::memory_order_release);
}

std::vector<Bot::TraceRow> Bot::traceSnapshot() const {
    const std::uint32_t h = traceHead_.load(std::memory_order_acquire);
    const std::uint32_t n = h < kTraceSize ? h : static_cast<std::uint32_t>(kTraceSize);
    std::vector<TraceRow> rows;
    rows.reserve(n);
    for (std::uint32_t i = h - n; i != h; ++i) rows.push_back(trace_[i % kTraceSize]);
    return rows;
}
void Bot::setMacro(Macro m) { std::lock_guard lk(mu_); macro_ = std::move(m); }

void Bot::run(std::stop_token st, Mode mode, InputKind kind, std::vector<MacroAction> actions, RefTrack ref, bool useYFix) {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const int vk = virtualKeyFor(kind);

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    auto nowTicks = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; };
    const long long t0 = nowTicks();
    auto msNow = [&] { return static_cast<double>(nowTicks() - t0) * 1000.0 / static_cast<double>(freq.QuadPart); };
    std::size_t idx = 0;
    float curX = 0, curY = 0;
    constexpr double kResetConfirmMs = 5.0;  // a drop in X must persist this long to count as a restart

    const bool soundOn = mode == Mode::Playing && clickSound.load();
    bool held = false;
    auto syncHold = [&](bool want) {
        if (want && !held) {
            if (!sendClick(kind, true)) setStatus("SendInput failed (elevation mismatch?)");
            held = true;
            ++clicksSent;
            if (mode == Mode::Playing) tracePush(msNow(), curX, curY, static_cast<int>(idx), 1);
            if (soundOn) sounder_.press();
        } else if (!want && held) {
            sendClick(kind, false);
            held = false;
            if (mode == Mode::Playing) tracePush(msNow(), curX, curY, static_cast<int>(idx), 2);
            if (soundOn) sounder_.release();
        }
    };

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
    bool needSeek = true;
    bool keyPrev = false, keyPrevValid = false;
    long long prevTick = 0;
    bool havePrevTick = false;
    double vel = 0.0;            // X units per millisecond, measured from observed X changes
    long long changeTick = 0;
    float changeX = 0;
    bool haveChange = false;
    float traceX = -1.0e9f;
    bool lowPending = false;
    long long lowSince = 0;
    float lastFixX = -1.0e9f;

    while (!st.stop_requested()) {
        float x = 0, y = 0;
        const ReadResult r = proc_.readPlayerPos(x, y);
        if (r == ReadResult::ProcessGone) { setStatus("game process exited"); break; }
        if (r != ReadResult::Ok) {
            if (mode == Mode::Playing) {
                syncHold(false);
                if (useIsolated) isolatedUp(true);
            }
            setStatus(std::string("waiting: ") + describe(r));
            havePrev = false; needSeek = true; keyPrevValid = false; havePrevTick = false;
            vel = 0.0; haveChange = false; estSpeed = 0.f;
            Sleep(5);
            continue;
        }
        {   // A one-off low read (seen right at a death) is ignored unless it persists.
            const long long tRead = nowTicks();
            if (havePrev && x < prevX - resetThreshold.load()) {
                if (!lowPending) { lowPending = true; lowSince = tRead; }
                if (static_cast<double>(tRead - lowSince) * 1000.0 / static_cast<double>(freq.QuadPart) < kResetConfirmMs) {
                    std::this_thread::yield();
                    continue;
                }
            } else {
                lowPending = false;
            }
        }
        lastX = x;
        lastY = y;
        curX = x;
        curY = y;
        {
            const long long t = nowTicks();
            if (havePrevTick && mode == Mode::Playing) {
                const auto gap = static_cast<std::uint32_t>((t - prevTick) * 1000000 / freq.QuadPart);
                if (gap > maxLoopGapUs.load()) maxLoopGapUs = gap;
            }
            prevTick = t;
            havePrevTick = true;

            if (!haveChange) {
                changeTick = t; changeX = x; haveChange = true;
            } else if (x != changeX) {
                const double dtMs = static_cast<double>(t - changeTick) * 1000.0 / static_cast<double>(freq.QuadPart);
                const double dx = static_cast<double>(x) - static_cast<double>(changeX);
                if (dtMs > 0.5 && dtMs < 50.0 && dx > 0.0) {  // skips restarts and pauses
                    const double v = dx / dtMs;
                    vel = (vel == 0.0) ? v : 0.7 * vel + 0.3 * v;
                    estSpeed = static_cast<float>(vel * 1000.0);
                }
                changeTick = t; changeX = x;
            }
            if (mode == Mode::Playing && x != traceX) {
                tracePush(static_cast<double>(t - t0) * 1000.0 / static_cast<double>(freq.QuadPart), x, y, static_cast<int>(idx), 0);
                traceX = x;
            }
        }
        const bool fg = proc_.isForeground();
        const bool reset = havePrev && x < prevX - resetThreshold.load();
        if (reset) ++attempts;
        if (reset && mode == Mode::Playing) tracePush(msNow(), x, y, static_cast<int>(idx), 3);
        if (reset && mode == Mode::Playing) {
            const DeathRecord d{prevX, lastFiredIdx.load(), lastFiredMacroX.load(), lastFiredActualX.load(),
                                static_cast<int>(idx), idx < actions.size() ? actions[idx].x : -1.f};
            std::lock_guard lk(mu_);
            if (deaths_.size() >= 50) deaths_.erase(deaths_.begin());
            deaths_.push_back(d);
        }
        if (reset) { vel = 0.0; haveChange = false; estSpeed = 0.f; }

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
                const double leadUnits = vel * static_cast<double>(leadMs.load());
                const float fire = x + static_cast<float>(leadUnits);
                const float staleLimit = 20.f + static_cast<float>(leadUnits < 0.0 ? -leadUnits : 0.0);
                bool skipped = false;
                // 1e-3 tolerance: macro X values are float32 and must not lose a whole step to rounding.
                while (idx < actions.size() && actions[idx].x <= fire + 1e-3f) {
                    if (x - actions[idx].x > staleLimit) {  // far behind: skip instead of bursting late clicks
                        skipped = true; ++staleSkipped; ++idx;
                        continue;
                    }
                    const bool press = actions[idx].down && !held;
                    syncHold(actions[idx].down);
                    lastFiredIdx = static_cast<int>(idx);
                    lastFiredMacroX = actions[idx].x;
                    lastFiredActualX = x;
                    if (press && useIsolated) isolatedDown();
                    ++idx;
                }
                if (skipped) syncHold(holdBefore(actions, idx));
                if (useYFix && x != lastFixX) {  // once per physics step, after the clicks have been handled
                    lastFixX = x;
                    float target = 0;
                    if (yFixTarget(ref, x, y, 0.01f, 12.f, target)) {
                        if (proc_.writePlayerYIf(x, target)) {
                            ++yFixWritten;
                            tracePush(msNow(), x, target, static_cast<int>(idx), 4);
                        } else {
                            ++yFixFailed;
                        }
                    }
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
