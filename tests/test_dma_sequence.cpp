#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

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
    mic.buf_1[i] = static_cast<uint16_t>((i + 1) * 10);
  }

  if (MicClass::completed_buffer_from_sequence(0) != mic.buf_0) {
    return 1;
  }

  if (MicClass::completed_buffer_from_sequence(1) != mic.buf_1) {
    return 2;
  }

  if (MicClass::buffer_index_from_sequence(0) != 0) {
    return 3;
  }

  if (MicClass::buffer_index_from_sequence(1) != 1) {
    return 4;
  }

  constexpr size_t kBytesToCopy = 4 * sizeof(uint16_t);
  std::array<uint16_t, 4> destination{};

  *MicClass::_buf_count_ptr = MicClass::buffer_index_from_sequence(0);
  mic.read(destination.data(), *MicClass::_buf_count_ptr, kBytesToCopy);
  if (!std::equal(destination.begin(), destination.end(), mic.buf_0)) {
    return 5;
  }

  *MicClass::_buf_count_ptr = MicClass::buffer_index_from_sequence(1);
  mic.read(destination.data(), *MicClass::_buf_count_ptr, kBytesToCopy);
  if (!std::equal(destination.begin(), destination.end(), mic.buf_1)) {
    return 6;
  }

  return 0;
}
