#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "hardware/base_mic.h"

int main() {
  mic_config_t config{};
  config.channel_cnt = 1;
  config.sampling_rate = 16000;
  config.buf_size = 4;
  config.debug_pin = 0;

  MicClass mic(&config);

  for (uint32_t i = 0; i < config.buf_size; ++i) {
    mic.buf_0[i] = static_cast<uint16_t>(i + 1);
  }

  std::array<uint16_t, 4> destination{};
  const size_t bytes_to_copy = destination.size() * sizeof(uint16_t);

  const int result = mic.read(destination.data(), /*buf_count=*/0, bytes_to_copy);

  if (result != static_cast<int>(bytes_to_copy)) {
    return 1;
  }

  if (!std::equal(destination.begin(), destination.end(), mic.buf_0)) {
    return 2;
  }

  return 0;
}
