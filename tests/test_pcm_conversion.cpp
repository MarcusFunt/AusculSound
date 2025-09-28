#include <cstdint>
#include <climits>

#include "pcm_conversion.h"

int main() {
  using namespace ausculsound;

  if (ConvertAdcSampleToPcm(0) != INT16_MIN) {
    return 1;
  }

  if (ConvertAdcSampleToPcm(kAdcMidpoint) != 0) {
    return 2;
  }

  const int16_t expected_max = ConvertAdcSampleToPcm(kAdcSampleMask);
  if (expected_max <= 0) {
    return 3;
  }

  if (ConvertAdcSampleToPcm(0xFFFFu) != expected_max) {
    return 4;
  }

  if (ConvertAdcSampleToPcm(kAdcSampleMask + 1u) != expected_max) {
    return 5;
  }

  return 0;
}

