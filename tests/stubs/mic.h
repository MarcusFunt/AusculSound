#pragma once

#include <cstddef>
#include <cstdint>

struct mic_config_t {
  uint8_t channel_cnt;
  uint32_t sampling_rate;
  std::size_t buf_size;
  uint8_t debug_pin;
};

class MG24_ADC_Class {
 public:
  explicit MG24_ADC_Class(mic_config_t *config) : config_(config) {}

  void set_callback(void (*callback)(uint16_t *, uint32_t)) { callback_ = callback; }

  bool begin() { return begin_result; }
  void end() {}
  void pause() {}
  void resume() {}

  void simulate_dma_completion(uint16_t *buffer, uint32_t length) {
    if (callback_) {
      callback_(buffer, length);
    }
  }

  mic_config_t *config() const { return config_; }

  bool begin_result = true;

 private:
  mic_config_t *config_;
  void (*callback_)(uint16_t *, uint32_t) = nullptr;
};

