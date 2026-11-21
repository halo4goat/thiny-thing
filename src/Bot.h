#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Input.h"
#include "Macro.h"
#include "Process.h"
#include "Sound.h"

class Bot {
public:
    enum class Mode { Idle, Recording, Playing };

    explicit Bot(GameProcess& p) : proc_(p) {}
    ~Bot() { stop(); }

    bool start(Mode m, InputKind kind, std::string& err);
    void stop();
    Mode mode() const { return mode_.load(); }
    std::string status() const;
    Macro macro() const;
    void setMacro(Macro m);

    // Uncalibrated: tune in the game.
    std::atomic<float> leadMs{0.f};            // fire this many milliseconds early (converted to X units with the measured speed)
    std::atomic<float> resetThreshold{30.f};   // X dropping by more than this = attempt restart
    std::atomic<bool> isolatedMouse{false};    // read once at start of playback
    std::atomic<float> isolatedHoldMs{5.f};
    std::atomic<bool> clickSound{false};       // read once at start of playback
    void testClickSound() { sounder_.press(); }

    std::atomic<std::uint64_t> clicksSent{0}, clicksRecorded{0}, attempts{0};
    std::atomic<std::uint64_t> isolatedSent{0}, isolatedSkipped{0};
    std::atomic<float> lastX{0.f};
    std::atomic<float> estSpeed{0.f};          // measured X units per second
    // Timing diagnostics for playback.
    std::atomic<std::uint32_t> maxLoopGapUs{0};   // longest gap between two successful X reads
    std::atomic<int> lastFiredIdx{-1};
    std::atomic<float> lastFiredMacroX{0.f}, lastFiredActualX{0.f};

    struct DeathRecord {
        float x;             // last X before the restart
        int lastIdx;         // last macro action fired before dying
        float lastMacroX;    // its X in the macro
        float lastActualX;   // the X that was read when it fired
        int nextIdx;         // next action that had not fired yet
        float nextMacroX;    // its X in the macro (-1 if none)
    };
    std::vector<DeathRecord> deaths() const;

    // Playback trace for debugging (ring buffer, newest rows kept).
    // event: 0 = X changed, 1 = press sent, 2 = release sent, 3 = restart detected
    struct TraceRow { double tMs; float x; int idx; int event; };
    std::vector<TraceRow> traceSnapshot() const;

private:
    void run(std::stop_token st, Mode m, InputKind kind, std::vector<MacroAction> actions);
    void setStatus(std::string s);

    GameProcess& proc_;
    ClickSounder sounder_;  // declared before worker_ so it outlives the playback thread
    std::jthread worker_;
    std::atomic<Mode> mode_{Mode::Idle};
    mutable std::mutex mu_;
    std::string status_ = "idle";
    Macro macro_;
    std::vector<DeathRecord> deaths_;

    static constexpr std::size_t kTraceSize = 8192;
    std::array<TraceRow, kTraceSize> trace_{};
    std::atomic<std::uint32_t> traceHead_{0};
    void tracePush(double tMs, float x, int idx, int event);
};
