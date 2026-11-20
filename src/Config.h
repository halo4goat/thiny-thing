#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct GameOffsets {
    std::string moduleName = "GeometryDash.exe";
    std::string buildNote;
    std::optional<std::uint64_t> moduleSize;      // value from the build the offsets were taken from
    std::optional<std::uint64_t> playerXBaseRva;  // RVA, not a file offset
    std::vector<std::uint64_t> playerXChain;      // empty = unset

    bool complete() const { return moduleSize && playerXBaseRva && !playerXChain.empty(); }
    std::string missing() const;
};

bool loadOffsets(const std::filesystem::path& path, GameOffsets& out, std::string& err);
