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

bool loadMacro(const std::filesystem::path& p, Macro& out, std::string& err);
bool saveMacro(const std::filesystem::path& p, const Macro& m, std::string& err);
