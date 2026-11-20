#include "Sound.h"

#include <mmsystem.h>

#include "ClickWav.h"

ClickSounder::ClickSounder() : press_(makeClickWav(true)), release_(makeClickWav(false)) {
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
    thread_ = std::thread([this] { run(); });
}

ClickSounder::~ClickSounder() {
    quit_ = true;
    if (event_) SetEvent(event_);
    if (thread_.joinable()) thread_.join();
    PlaySoundW(nullptr, nullptr, 0);  // stop anything still playing before the buffers go away
    if (event_) CloseHandle(event_);
}

void ClickSounder::request(unsigned bit) {
    flags_.fetch_or(bit);
    if (event_) SetEvent(event_);
}

void ClickSounder::run() {
    while (true) {
        WaitForSingleObject(event_, INFINITE);
        if (quit_) break;
        const unsigned f = flags_.exchange(0);
        const std::vector<unsigned char>* wav = (f & 1) ? &press_ : ((f & 2) ? &release_ : nullptr);
        if (wav) PlaySoundW(reinterpret_cast<LPCWSTR>(wav->data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
}
