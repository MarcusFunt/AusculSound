#pragma once

#include <cstddef>
#include <cstdint>

using uint8_t = std::uint8_t;
using uint16_t = std::uint16_t;
using uint32_t = std::uint32_t;

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}

constexpr uint8_t OUTPUT = 0x1;
