#include "Macro.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
constexpr std::size_t kMaxActions = 1'000'000;
constexpr const char* kHeader = "GDMACRO1";
}

std::size_t seekIndex(const std::vector<MacroAction>& a, float x) {
    auto it = std::lower_bound(a.begin(), a.end(), x,
                               [](const MacroAction& m, float v) { return m.x < v; });
    return static_cast<std::size_t>(it - a.begin());
}

bool holdBefore(const std::vector<MacroAction>& a, std::size_t idx) {
    return idx > 0 && idx <= a.size() && a[idx - 1].down;
}

bool loadMacro(const std::filesystem::path& p, Macro& out, std::string& err) {
    std::ifstream f(p);
    if (!f) { err = "cannot open " + p.string(); return false; }
    std::string line;
    if (!std::getline(f, line) || line.rfind(kHeader, 0) != 0) {
        err = "not a GDMACRO1 file";
        return false;
    }
    Macro m;
    float prevX = -1.0e30f;
    bool expectDown = true;
    int lineNo = 1;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty()) continue;
        std::istringstream ss(line);
        float x = 0; int d = -1;
        if (!(ss >> x >> d) || (d != 0 && d != 1) || !std::isfinite(x)) {
            err = "line " + std::to_string(lineNo) + ": expected '<x> <0|1>'";
            return false;
        }
        if (x < prevX) { err = "line " + std::to_string(lineNo) + ": x goes backwards"; return false; }
        if ((d == 1) != expectDown) { err = "line " + std::to_string(lineNo) + ": press/release must alternate"; return false; }
        if (m.actions.size() >= kMaxActions) { err = "too many actions"; return false; }
        m.actions.push_back({x, d == 1});
        prevX = x;
        expectDown = !expectDown;
    }
    out = std::move(m);
    return true;
}

bool saveMacro(const std::filesystem::path& p, const Macro& m, std::string& err) {
    std::ofstream f(p, std::ios::trunc);
    if (!f) { err = "cannot write " + p.string(); return false; }
    f << kHeader << '\n' << std::setprecision(9);  // 9 significant digits round-trip a float exactly
    for (const auto& a : m.actions) f << a.x << ' ' << (a.down ? 1 : 0) << '\n';
    if (!f) { err = "write failed"; return false; }
    return true;
}
