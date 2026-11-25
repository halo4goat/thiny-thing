#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "Config.h"

enum class ReadResult {
    Ok, NotAttached, OffsetsIncomplete, ProcessGone, ReadFailed, ImplausiblePointer, ImplausibleValue
};
const char* describe(ReadResult r);

// attach()/detach() must only be called while no other thread is reading.
class GameProcess {
public:
    ~GameProcess() { detach(); }
    bool attach(const GameOffsets& offsets, std::string& err);
    void detach();
    bool attached() const { return handle_ != nullptr; }
    bool alive() const;
    bool isForeground() const;
    bool cursorOverGame() const;
    bool canWrite() const { return canWrite_; }
    // Rewrites the player's Y, but only if X is still expectedX (the game has not stepped since it was read).
    bool writePlayerYIf(float expectedX, float newY);
    ReadResult readPlayerX(float& x) const;
    ReadResult readPlayerPos(float& x, float& y) const;  // y is NaN if the float next to X is not finite
    std::uint64_t observedModuleSize() const { return size_; }

private:
    bool readRaw(std::uint64_t addr, void* buf, std::size_t n) const;

    HANDLE handle_ = nullptr;
    bool canWrite_ = false;
    mutable std::atomic<std::uint64_t> posAddr_{0};  // address of the player's node X, refreshed by every read
    DWORD pid_ = 0;
    std::uint64_t base_ = 0;
    std::uint64_t size_ = 0;
    GameOffsets offsets_;
};
