#pragma once
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
    std::atomic<float> leadUnits{0.f};         // fire this many X-units early to cover input latency
    std::atomic<float> resetThreshold{30.f};   // X dropping by more than this = attempt restart
    std::atomic<bool> isolatedMouse{false};    // read once at start of playback
    std::atomic<float> isolatedHoldMs{5.f};
    std::atomic<bool> clickSound{false};       // read once at start of playback
    void testClickSound() { sounder_.press(); }

    std::atomic<std::uint64_t> clicksSent{0}, clicksRecorded{0}, attempts{0};
    std::atomic<std::uint64_t> isolatedSent{0}, isolatedSkipped{0};
    std::atomic<float> lastX{0.f};
    // Timing diagnostics for playback.
    std::atomic<std::uint32_t> maxLoopGapUs{0};   // longest gap between two successful X reads
    std::atomic<int> lastFiredIdx{-1};
    std::atomic<float> lastFiredMacroX{0.f}, lastFiredActualX{0.f};

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
};
