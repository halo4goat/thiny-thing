#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool parseU64(const std::string& s, std::uint64_t& out) {
    if (s.empty()) return false;
    try {
        std::size_t used = 0;
        out = std::stoull(s, &used, 0);  // base 0: accepts 0x... and decimal
        return used == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

std::string GameOffsets::missing() const {
    std::string m;
    if (!moduleSize) m += "module_size ";
    if (!playerXBaseRva) m += "player_x_base_rva ";
    if (playerXChain.empty()) m += "player_x_chain ";
    return m;
}

bool loadOffsets(const std::filesystem::path& path, GameOffsets& out, std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    GameOffsets r;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        line = trim(line);
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            err = "line " + std::to_string(lineNo) + ": expected key=value";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty()) continue;  // unset

        std::uint64_t n = 0;
        if (key == "module") {
            r.moduleName = val;
        } else if (key == "build_note") {
            r.buildNote = val;
        } else if (key == "module_size" || key == "player_x_base_rva") {
            if (!parseU64(val, n)) {
                err = "line " + std::to_string(lineNo) + ": bad number for " + key;
                return false;
            }
            (key == "module_size" ? r.moduleSize : r.playerXBaseRva) = n;
        } else if (key == "player_x_chain") {
            std::size_t start = 0;
            while (start <= val.size()) {
                auto comma = val.find(',', start);
                std::string tok = trim(val.substr(start, comma == std::string::npos ? comma : comma - start));
                if (!parseU64(tok, n)) {
                    err = "line " + std::to_string(lineNo) + ": bad chain element '" + tok + "'";
                    return false;
                }
                r.playerXChain.push_back(n);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else {
            err = "line " + std::to_string(lineNo) + ": unknown key " + key;
            return false;
        }
    }
    out = std::move(r);
    return true;
}
