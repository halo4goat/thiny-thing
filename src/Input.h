#pragma once

enum class InputKind { Space = 0, Up = 1, Mouse = 2 };

int virtualKeyFor(InputKind k);        // VK_LBUTTON for Mouse
bool sendClick(InputKind k, bool down);
