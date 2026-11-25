#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct MacroAction {
    float x;
    bool down;
};

struct Macro {
    std::vector<MacroAction> actions;
};

std::size_t seekIndex(const std::vector<MacroAction>& a, float x);
bool holdBefore(const std::vector<MacroAction>& a, std::size_t idx);

// Per-frame reference positions of the recorded run (from the replay's frame fixes).
struct RefTrack {
    std::vector<float> x, y;  // x is non-decreasing
    bool empty() const { return x.empty(); }
};
bool loadRef(const std::filesystem::path& p, RefTrack& out, std::string& err);
// Decides whether the player's Y should be rewritten. Returns false (and writes nothing) when X is not on
// the reference grid, when two reference frames share this X (ambiguous), when Y is already within minDiff,
// or when it is further than maxDiff away (probably a wrong match, or a death).
bool yFixTarget(const RefTrack& r, float x, float y, float minDiff, float maxDiff, float& target);

bool loadMacro(const std::filesystem::path& p, Macro& out, std::string& err);
bool saveMacro(const std::filesystem::path& p, const Macro& m, std::string& err);
