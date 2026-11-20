#pragma once
#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

// Plays a click on its own thread so the playback loop never waits on audio.
class ClickSounder {
public:
    ClickSounder();
    ~ClickSounder();
    ClickSounder(const ClickSounder&) = delete;
    ClickSounder& operator=(const ClickSounder&) = delete;

    void press() { request(1); }
    void release() { request(2); }

private:
    void request(unsigned bit);
    void run();

    std::vector<unsigned char> press_, release_;
    std::atomic<unsigned> flags_{0};
    std::atomic<bool> quit_{false};
    HANDLE event_ = nullptr;
    std::thread thread_;
};
