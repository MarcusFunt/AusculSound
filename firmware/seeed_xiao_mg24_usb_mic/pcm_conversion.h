#pragma once

#include <cstdint>
#include <climits>

namespace ausculsound {

// The MG24 ADC produces 12-bit unsigned samples that we centre around zero and
// scale to 16-bit signed PCM for USB transmission.
constexpr uint8_t kAdcResolutionBits = 12;
constexpr int16_t kAdcMidpoint = 1 << (kAdcResolutionBits - 1);  // 2048
constexpr uint8_t kAdcToPcmShift = 16 - kAdcResolutionBits;      // Shift left by 4
constexpr uint16_t kAdcSampleMask = (1u << kAdcResolutionBits) - 1u;

inline int16_t ConvertAdcSampleToPcm(uint16_t sample) {
  const uint16_t masked_sample = sample & kAdcSampleMask;
  int32_t centered = static_cast<int32_t>(masked_sample) - kAdcMidpoint;
  int32_t scaled = centered * (1 << kAdcToPcmShift);

  if (scaled > INT16_MAX) {
    scaled = INT16_MAX;
  } else if (scaled < INT16_MIN) {
    scaled = INT16_MIN;
  }

  return static_cast<int16_t>(scaled);
}

}  // namespace ausculsound

