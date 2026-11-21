#pragma once
#include <windows.h>

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
    ReadResult readPlayerX(float& x) const;
    ReadResult readPlayerPos(float& x, float& y) const;  // y is NaN if the float next to X is not finite
    std::uint64_t observedModuleSize() const { return size_; }

private:
    bool readRaw(std::uint64_t addr, void* buf, std::size_t n) const;

    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
    std::uint64_t base_ = 0;
    std::uint64_t size_ = 0;
    GameOffsets offsets_;
};
