#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#define ARDUINO_XIAO_MG24
#define F(x) x

#include "stubs/Arduino.h"
#include "stubs/mic.h"

namespace {
struct MockSerial {
  void begin(unsigned long) {}
  explicit operator bool() const { return ready; }

  template <typename T>
  void print(const T &) {}

  template <typename T>
  void println(const T &) {}

  void println() {}

  std::size_t write(const uint8_t *data, std::size_t length) {
    last_write.assign(data, data + length);
    return length;
  }

  std::size_t write(uint8_t *data, std::size_t length) {
    last_write.assign(data, data + length);
    return length;
  }

  void reset() {
    ready = true;
    last_write.clear();
  }

  bool ready = true;
  std::vector<uint8_t> last_write;
};

MockSerial mock_usb_serial;
}  // namespace

uint32_t g_millis = 0;

uint32_t millis() { return g_millis; }

void delay(uint32_t) {}

void noInterrupts() {}

void interrupts() {}

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define USBCDC_SERIAL_PORT mock_usb_serial

#include "../firmware/seeed_xiao_mg24_usb_mic/seeed_xiao_mg24_usb_mic.ino"

#undef min
#undef max

namespace {
void ResetFirmwareState() {
  mock_usb_serial.reset();
  s_samplesReady = false;
  s_samplesCaptured = 0;
  s_callbackCount = 0;
  s_streamedBlocks = 0;
  s_lastLoopEntryMs = 0;
  g_millis = 0;
}

bool ExpectCaptureBufferEquals(const uint16_t *expected, std::size_t count) {
  return std::equal(expected, expected + count, s_captureBuffer);
}
}

int main() {
  ResetFirmwareState();

  if (convertAdcSampleToPcm(kAdcMidpoint) != 0) {
    return 1;
  }
  if (convertAdcSampleToPcm(0) != std::numeric_limits<int16_t>::min()) {
    return 2;
  }
  if (convertAdcSampleToPcm((1u << 12) - 1) != 32752) {
    return 3;
  }

  ResetFirmwareState();
  std::array<uint16_t, NUM_SAMPLES * 2> oversized_buffer{};
  for (std::size_t i = 0; i < oversized_buffer.size(); ++i) {
    oversized_buffer[i] = static_cast<uint16_t>(i);
  }
  onSamplesReady(oversized_buffer.data(), static_cast<uint32_t>(oversized_buffer.size()));
  if (!s_samplesReady) {
    return 4;
  }
  if (s_samplesCaptured != NUM_SAMPLES) {
    return 5;
  }
  if (!ExpectCaptureBufferEquals(oversized_buffer.data(), NUM_SAMPLES)) {
    return 6;
  }
  if (s_callbackCount != 1) {
    return 7;
  }

  ResetFirmwareState();
  std::array<uint16_t, 4> raw_samples{0u, static_cast<uint16_t>(kAdcMidpoint),
                                      static_cast<uint16_t>(kAdcMidpoint + 1),
                                      static_cast<uint16_t>((1u << 12) - 1)};

  streamPcmBlock(raw_samples.data(), raw_samples.size());
  if (mock_usb_serial.last_write.size() != raw_samples.size() * sizeof(int16_t)) {
    return 8;
  }

  std::array<int16_t, raw_samples.size()> pcm_values{};
  std::memcpy(pcm_values.data(), mock_usb_serial.last_write.data(), mock_usb_serial.last_write.size());
  for (std::size_t i = 0; i < raw_samples.size(); ++i) {
    if (pcm_values[i] != convertAdcSampleToPcm(raw_samples[i])) {
      return static_cast<int>(9 + i);
    }
  }
  if (s_streamedBlocks != 1) {
    return 13;
  }

  return 0;
}
