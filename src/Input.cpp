#include "Input.h"

#include <windows.h>

int virtualKeyFor(InputKind k) {
    switch (k) {
        case InputKind::Space: return VK_SPACE;
        case InputKind::Up: return VK_UP;
        case InputKind::Mouse: return VK_LBUTTON;
    }
    return VK_SPACE;
}

bool sendClick(InputKind k, bool down) {
    INPUT in{};
    if (k == InputKind::Mouse) {
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else {
        const UINT vk = static_cast<UINT>(virtualKeyFor(k));
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = static_cast<WORD>(vk);
        in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (k == InputKind::Up ? KEYEVENTF_EXTENDEDKEY : 0);
    }
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}
