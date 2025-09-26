#include <mic.h>

#include <cstring>
#include <climits>

// Audio configuration. Increase NUM_SAMPLES to trade latency for throughput.
constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t NUM_SAMPLES = 256;

// Enable this flag to print verbose diagnostics alongside the audio stream.
// WARNING: Any USB_SERIAL.print() calls will interleave ASCII text with the
// raw PCM bytes, which will be heard as loud static by host applications that
// expect pure audio data. Leave this disabled for normal streaming.
constexpr bool kEnableDebugLogging = false;

#define DEBUG_PRINT(...)                                                               \
  do {                                                                                 \
    if (kEnableDebugLogging) {                                                         \
      USB_SERIAL.print(__VA_ARGS__);                                                   \
    }                                                                                  \
  } while (0)

#define DEBUG_PRINTLN(...)                                                             \
  do {                                                                                 \
    if (kEnableDebugLogging) {                                                         \
      USB_SERIAL.println(__VA_ARGS__);                                                 \
    }                                                                                  \
  } while (0)

// The ADC on the MG24 delivers 12-bit samples. Convert them to signed 16-bit
// PCM centred around zero for USB transmission.
constexpr int16_t kAdcMidpoint = 1 << 11;   // 2048
constexpr uint8_t kAdcToPcmShift = 16 - 12; // Shift left by 4 bits

// Helper macro to pick the correct USB CDC serial port symbol provided by the
// Seeed Studio MG24 Arduino core.
#if defined(USBCDC_SERIAL_PORT)
#define USB_SERIAL USBCDC_SERIAL_PORT
#else
#define USB_SERIAL Serial
#endif

// Local copies of each DMA block and the converted PCM data that is written
// to the USB serial connection.
static uint16_t s_captureBuffer[NUM_SAMPLES];
static uint16_t s_processingBuffer[NUM_SAMPLES];
static int16_t s_pcmBuffer[NUM_SAMPLES];

static volatile bool s_samplesReady = false;
static volatile size_t s_samplesCaptured = 0;
static volatile size_t s_callbackCount = 0;

static size_t s_streamedBlocks = 0;
static uint32_t s_lastLoopEntryMs = 0;

static mic_config_t s_micConfig{
    .channel_cnt = 1,
    .sampling_rate = kSampleRateHz,
    .buf_size = NUM_SAMPLES,
    .debug_pin = 0,
};

static MG24_ADC_Class s_mic(&s_micConfig);

static void onSamplesReady(uint16_t *buffer, uint32_t length);
static void streamPcmBlock(const uint16_t *buffer, size_t samples);
static void runSelfTests();
static bool testAdcConversionRange();
static bool testMicConfig();
static bool testBufferSizes();
static bool testPcmSignedRange();
static void logBufferStatistics(const uint16_t *buffer, size_t samples);
static void logPcmStatistics(const int16_t *buffer, size_t samples);
static int16_t convertAdcSampleToPcm(uint16_t sample);

void setup() {
  USB_SERIAL.begin(115200);
  uint32_t start = millis();
  while (!USB_SERIAL && (millis() - start < 5000)) {
    delay(10);
  }

  DEBUG_PRINTLN(F("Beginning self-test suite..."));
  runSelfTests();

  s_mic.set_callback(onSamplesReady);
  if (!s_mic.begin()) {
    DEBUG_PRINTLN(F("Microphone init failed"));
    while (true) {
      delay(1000);
    }
  }

  DEBUG_PRINTLN(F("XIAO MG24 microphone streaming over USB CDC"));
  DEBUG_PRINTLN(F("Source: Seeed Arduino Mic DMA"));
  DEBUG_PRINT(F("Configuration - Sample Rate: "));
  DEBUG_PRINTLN(kSampleRateHz);
  DEBUG_PRINT(F("Configuration - Num Samples per block: "));
  DEBUG_PRINTLN(NUM_SAMPLES);
  DEBUG_PRINT(F("Configuration - ADC Midpoint: "));
  DEBUG_PRINTLN(kAdcMidpoint);
  DEBUG_PRINT(F("Configuration - ADC shift: "));
  DEBUG_PRINTLN(kAdcToPcmShift);
  DEBUG_PRINTLN(F("Setup complete. Awaiting audio samples..."));
}

void loop() {
  size_t samples = 0;

  const uint32_t nowMs = millis();
  if (nowMs - s_lastLoopEntryMs > 1000) {
    DEBUG_PRINT(F("Loop heartbeat - ms since last log: "));
    DEBUG_PRINTLN(nowMs - s_lastLoopEntryMs);
    DEBUG_PRINT(F("Callback count so far: "));
    DEBUG_PRINTLN(s_callbackCount);
    DEBUG_PRINT(F("Blocks streamed so far: "));
    DEBUG_PRINTLN(s_streamedBlocks);
  }
  s_lastLoopEntryMs = nowMs;

  noInterrupts();
  if (s_samplesReady) {
    samples = min(s_samplesCaptured, static_cast<size_t>(NUM_SAMPLES));
    memcpy(s_processingBuffer, s_captureBuffer, samples * sizeof(uint16_t));
    s_samplesReady = false;
  }
  interrupts();

  if (samples == 0) {
    return;
  }

  streamPcmBlock(s_processingBuffer, samples);
}

static void onSamplesReady(uint16_t *buffer, uint32_t length) {
  if (length == 0) {
    DEBUG_PRINTLN(F("onSamplesReady invoked with zero length buffer"));
    return;
  }

  size_t samples = min(static_cast<size_t>(length), NUM_SAMPLES);
  memcpy(s_captureBuffer, buffer, samples * sizeof(uint16_t));

  s_samplesCaptured = samples;
  s_samplesReady = true;
  ++s_callbackCount;

  DEBUG_PRINT(F("onSamplesReady - raw length: "));
  DEBUG_PRINTLN(length);
  DEBUG_PRINT(F("onSamplesReady - samples copied: "));
  DEBUG_PRINTLN(samples);
}

static void streamPcmBlock(const uint16_t *buffer, size_t samples) {
  if (samples == 0) {
    DEBUG_PRINTLN(F("streamPcmBlock invoked with zero samples"));
    return;
  }

  logBufferStatistics(buffer, samples);

  for (size_t i = 0; i < samples; ++i) {
    s_pcmBuffer[i] = convertAdcSampleToPcm(buffer[i]);
  }

  const size_t bytesToWrite = samples * sizeof(s_pcmBuffer[0]);
  USB_SERIAL.write(reinterpret_cast<uint8_t *>(s_pcmBuffer), bytesToWrite);

  logPcmStatistics(s_pcmBuffer, samples);

  ++s_streamedBlocks;
  DEBUG_PRINT(F("streamPcmBlock - block index: "));
  DEBUG_PRINTLN(s_streamedBlocks);
  DEBUG_PRINT(F("streamPcmBlock - bytes written: "));
  DEBUG_PRINTLN(bytesToWrite);
}

static void runSelfTests() {
  if (!kEnableDebugLogging) {
    return;
  }
  bool allPassed = true;

  if (testAdcConversionRange()) {
    DEBUG_PRINTLN(F("Self-test: ADC conversion range - PASS"));
  } else {
    DEBUG_PRINTLN(F("Self-test: ADC conversion range - FAIL"));
    allPassed = false;
  }

  if (testMicConfig()) {
    DEBUG_PRINTLN(F("Self-test: Microphone configuration - PASS"));
  } else {
    DEBUG_PRINTLN(F("Self-test: Microphone configuration - FAIL"));
    allPassed = false;
  }

  if (testBufferSizes()) {
    DEBUG_PRINTLN(F("Self-test: Buffer sizes - PASS"));
  } else {
    DEBUG_PRINTLN(F("Self-test: Buffer sizes - FAIL"));
    allPassed = false;
  }

  if (testPcmSignedRange()) {
    DEBUG_PRINTLN(F("Self-test: PCM signed range - PASS"));
  } else {
    DEBUG_PRINTLN(F("Self-test: PCM signed range - FAIL"));
    allPassed = false;
  }

  if (allPassed) {
    DEBUG_PRINTLN(F("All self-tests passed."));
  } else {
    DEBUG_PRINTLN(F("One or more self-tests failed. Streaming will continue with caution."));
  }
}

static bool testAdcConversionRange() {
  if (!kEnableDebugLogging) {
    return true;
  }
  bool success = true;

  const int16_t minSample = convertAdcSampleToPcm(0);
  const int16_t midSample = convertAdcSampleToPcm(kAdcMidpoint);
  const int16_t maxSample = convertAdcSampleToPcm((1u << 12) - 1);

  DEBUG_PRINT(F("Test ADC Conversion - min raw 0 -> "));
  DEBUG_PRINTLN(minSample);
  DEBUG_PRINT(F("Test ADC Conversion - midpoint raw -> "));
  DEBUG_PRINTLN(midSample);
  DEBUG_PRINT(F("Test ADC Conversion - max raw -> "));
  DEBUG_PRINTLN(maxSample);

  if (midSample != 0) {
    DEBUG_PRINTLN(F("Expected midpoint to convert to 0"));
    success = false;
  }
  if (minSample >= 0) {
    DEBUG_PRINTLN(F("Expected min sample to be negative"));
    success = false;
  }
  if (maxSample <= 0) {
    DEBUG_PRINTLN(F("Expected max sample to be positive"));
    success = false;
  }

  return success;
}

static bool testMicConfig() {
  if (!kEnableDebugLogging) {
    return true;
  }
  bool success = true;

  if (s_micConfig.channel_cnt != 1) {
    DEBUG_PRINT(F("Unexpected channel count: "));
    DEBUG_PRINTLN(s_micConfig.channel_cnt);
    success = false;
  }
  if (s_micConfig.sampling_rate != kSampleRateHz) {
    DEBUG_PRINT(F("Unexpected sampling rate: "));
    DEBUG_PRINTLN(s_micConfig.sampling_rate);
    success = false;
  }
  if (s_micConfig.buf_size != NUM_SAMPLES) {
    DEBUG_PRINT(F("Unexpected buffer size: "));
    DEBUG_PRINTLN(s_micConfig.buf_size);
    success = false;
  }

  DEBUG_PRINT(F("Mic config debug - channel count: "));
  DEBUG_PRINTLN(s_micConfig.channel_cnt);
  DEBUG_PRINT(F("Mic config debug - sampling rate: "));
  DEBUG_PRINTLN(s_micConfig.sampling_rate);
  DEBUG_PRINT(F("Mic config debug - buffer size: "));
  DEBUG_PRINTLN(s_micConfig.buf_size);
  DEBUG_PRINT(F("Mic config debug - debug pin: "));
  DEBUG_PRINTLN(s_micConfig.debug_pin);

  return success;
}

static bool testBufferSizes() {
  if (!kEnableDebugLogging) {
    return true;
  }
  bool success = true;

  const size_t captureCount = sizeof(s_captureBuffer) / sizeof(s_captureBuffer[0]);
  const size_t processingCount = sizeof(s_processingBuffer) / sizeof(s_processingBuffer[0]);
  const size_t pcmCount = sizeof(s_pcmBuffer) / sizeof(s_pcmBuffer[0]);

  DEBUG_PRINT(F("Buffer size debug - capture count: "));
  DEBUG_PRINTLN(captureCount);
  DEBUG_PRINT(F("Buffer size debug - processing count: "));
  DEBUG_PRINTLN(processingCount);
  DEBUG_PRINT(F("Buffer size debug - pcm count: "));
  DEBUG_PRINTLN(pcmCount);

  if (captureCount != NUM_SAMPLES) {
    success = false;
  }
  if (processingCount != NUM_SAMPLES) {
    success = false;
  }
  if (pcmCount != NUM_SAMPLES) {
    success = false;
  }

  return success;
}

static bool testPcmSignedRange() {
  if (!kEnableDebugLogging) {
    return true;
  }
  bool success = true;

  int16_t minValue = INT16_MAX;
  int16_t maxValue = INT16_MIN;

  for (uint16_t raw = 0; raw < (1u << 12); ++raw) {
    const int16_t pcm = convertAdcSampleToPcm(raw);
    if (pcm < minValue) {
      minValue = pcm;
    }
    if (pcm > maxValue) {
      maxValue = pcm;
    }
  }

  DEBUG_PRINT(F("PCM range debug - min value: "));
  DEBUG_PRINTLN(minValue);
  DEBUG_PRINT(F("PCM range debug - max value: "));
  DEBUG_PRINTLN(maxValue);

  if (minValue < INT16_MIN) {
    DEBUG_PRINTLN(F("PCM minimum exceeded INT16_MIN"));
    success = false;
  }
  if (maxValue > INT16_MAX) {
    DEBUG_PRINTLN(F("PCM maximum exceeded INT16_MAX"));
    success = false;
  }

  if (minValue >= maxValue) {
    DEBUG_PRINTLN(F("PCM min should be less than max"));
    success = false;
  }

  return success;
}

static void logBufferStatistics(const uint16_t *buffer, size_t samples) {
  if (!kEnableDebugLogging) {
    return;
  }
  uint16_t minValue = UINT16_MAX;
  uint16_t maxValue = 0;
  uint32_t sum = 0;

  for (size_t i = 0; i < samples; ++i) {
    const uint16_t sample = buffer[i];
    minValue = min(minValue, sample);
    maxValue = max(maxValue, sample);
    sum += sample;
  }

  const uint32_t average = samples > 0 ? sum / samples : 0;

  DEBUG_PRINT(F("Capture stats - min: "));
  DEBUG_PRINT(minValue);
  DEBUG_PRINT(F(", max: "));
  DEBUG_PRINT(maxValue);
  DEBUG_PRINT(F(", avg: "));
  DEBUG_PRINTLN(average);
}

static void logPcmStatistics(const int16_t *buffer, size_t samples) {
  if (!kEnableDebugLogging) {
    return;
  }
  int16_t minValue = INT16_MAX;
  int16_t maxValue = INT16_MIN;
  int32_t sum = 0;

  for (size_t i = 0; i < samples; ++i) {
    const int16_t sample = buffer[i];
    minValue = min(minValue, sample);
    maxValue = max(maxValue, sample);
    sum += sample;
  }

  const int32_t average = samples > 0 ? sum / static_cast<int32_t>(samples) : 0;

  DEBUG_PRINT(F("PCM stats - min: "));
  DEBUG_PRINT(minValue);
  DEBUG_PRINT(F(", max: "));
  DEBUG_PRINT(maxValue);
  DEBUG_PRINT(F(", avg: "));
  DEBUG_PRINTLN(average);
}

static int16_t convertAdcSampleToPcm(uint16_t sample) {
  int32_t value = static_cast<int32_t>(sample);
  value -= kAdcMidpoint;
  value <<= kAdcToPcmShift;
  return static_cast<int16_t>(value);
}
