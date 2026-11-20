#include "Process.h"

#include <tlhelp32.h>

#include <cmath>

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

DWORD findPid(const std::wstring& exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, exe.c_str()) == 0) { pid = pe.th32ProcessID; break; }
    }
    CloseHandle(snap);
    return pid;
}

bool findModule(DWORD pid, const std::wstring& name, std::uint64_t& base, std::uint64_t& size) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        if (_wcsicmp(me.szModule, name.c_str()) == 0) {
            base = reinterpret_cast<std::uint64_t>(me.modBaseAddr);
            size = me.modBaseSize;
            found = true;
            break;
        }
    }
    CloseHandle(snap);
    return found;
}

bool plausiblePtr(std::uint64_t p) { return p >= 0x10000 && p < 0x00007FFFFFFE0000ULL; }

}  // namespace

const char* describe(ReadResult r) {
    switch (r) {
        case ReadResult::Ok: return "ok";
        case ReadResult::NotAttached: return "not attached";
        case ReadResult::OffsetsIncomplete: return "offsets incomplete";
        case ReadResult::ProcessGone: return "game process exited";
        case ReadResult::ReadFailed: return "memory read failed";
        case ReadResult::ImplausiblePointer: return "pointer in chain is null/implausible";
        case ReadResult::ImplausibleValue: return "player X is not a plausible float";
    }
    return "?";
}

bool GameProcess::attach(const GameOffsets& o, std::string& err) {
    detach();
    DWORD pid = findPid(toWide(o.moduleName));
    if (!pid) { err = o.moduleName + " is not running"; return false; }

    std::uint64_t base = 0, size = 0;
    if (!findModule(pid, toWide(o.moduleName), base, size)) {
        err = "module snapshot failed (run both programs with the same elevation)";
        return false;
    }
    if (o.moduleSize && *o.moduleSize != size) {
        err = "module size mismatch: config build has " + std::to_string(*o.moduleSize) +
              ", running game has " + std::to_string(size) + ". Offsets are for another build.";
        return false;
    }
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) { err = "OpenProcess failed, error " + std::to_string(GetLastError()); return false; }

    handle_ = h; pid_ = pid; base_ = base; size_ = size; offsets_ = o;
    return true;
}

void GameProcess::detach() {
    if (handle_) CloseHandle(handle_);
    handle_ = nullptr; pid_ = 0; base_ = size_ = 0;
}

bool GameProcess::alive() const {
    return handle_ && WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT;
}

bool GameProcess::isForeground() const {
    HWND fg = GetForegroundWindow();
    if (!fg || !pid_) return false;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(fg, &fgPid);
    return fgPid == pid_;
}

bool GameProcess::cursorOverGame() const {
    POINT p{};
    if (!pid_ || !GetCursorPos(&p)) return false;
    HWND w = WindowFromPoint(p);
    if (!w) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(w, &owner);
    return owner == pid_;
}

bool GameProcess::readRaw(std::uint64_t addr, void* buf, std::size_t n) const {
    SIZE_T got = 0;
    return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr), buf, n, &got) && got == n;
}

ReadResult GameProcess::readPlayerX(float& x) const {
    if (!handle_) return ReadResult::NotAttached;
    if (!alive()) return ReadResult::ProcessGone;
    if (!offsets_.complete()) return ReadResult::OffsetsIncomplete;

    const auto& chain = offsets_.playerXChain;
    if (*offsets_.playerXBaseRva + sizeof(std::uint64_t) > size_) return ReadResult::ImplausiblePointer;

    std::uint64_t cur = base_ + *offsets_.playerXBaseRva;
    if (!readRaw(cur, &cur, sizeof(cur))) return ReadResult::ReadFailed;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        if (!plausiblePtr(cur)) return ReadResult::ImplausiblePointer;
        if (!readRaw(cur + chain[i], &cur, sizeof(cur))) return ReadResult::ReadFailed;
    }
    if (!plausiblePtr(cur)) return ReadResult::ImplausiblePointer;

    float v = 0;
    if (!readRaw(cur + chain.back(), &v, sizeof(v))) return ReadResult::ReadFailed;
    if (!std::isfinite(v) || std::fabs(v) > 1.0e7f) return ReadResult::ImplausibleValue;
    x = v;
    return ReadResult::Ok;
}
