#pragma once
// Portable (no Windows headers) so the generated WAV can be checked off-target.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Short synthetic click: 44.1 kHz, 16-bit mono PCM in a WAV container. press = sharper, release = softer.
inline std::vector<unsigned char> makeClickWav(bool press) {
    constexpr std::uint32_t rate = 44100;
    const std::uint32_t n = rate * 18 / 1000;  // 18 ms
    std::vector<std::int16_t> pcm(n);
    std::uint32_t seed = press ? 12345u : 54321u;
    const double tau = press ? 0.0018 : 0.0030;
    const double freq = press ? 1900.0 : 1100.0;
    const double amp = press ? 0.85 : 0.55;
    for (std::uint32_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate;
        seed = seed * 1664525u + 1013904223u;
        const double noise = static_cast<double>(seed >> 8) / 8388608.0 - 1.0;  // -1..1
        const double env = std::exp(-t / tau);
        const double s = amp * env * (0.6 * noise + 0.4 * std::sin(6.283185307179586 * freq * t));
        pcm[i] = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0, 1.0) * 32000.0));
    }
    const std::uint32_t dataBytes = n * 2;
    std::vector<unsigned char> w(44 + dataBytes);
    auto put32 = [&](std::size_t o, std::uint32_t v) { std::memcpy(&w[o], &v, 4); };  // Windows is little-endian
    auto put16 = [&](std::size_t o, std::uint16_t v) { std::memcpy(&w[o], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4);  put32(4, 36 + dataBytes);  std::memcpy(&w[8], "WAVE", 4);
    std::memcpy(&w[12], "fmt ", 4); put32(16, 16); put16(20, 1); put16(22, 1);
    put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&w[36], "data", 4); put32(40, dataBytes);
    std::memcpy(&w[44], pcm.data(), dataBytes);
    return w;
}
