#include <mic.h>

#include <cstring>
#include <climits>

// Audio configuration. Increase NUM_SAMPLES to trade latency for throughput.
constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t NUM_SAMPLES = 256;

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

  USB_SERIAL.println(F("Beginning self-test suite..."));
  runSelfTests();

  s_mic.set_callback(onSamplesReady);
  if (!s_mic.begin()) {
    USB_SERIAL.println(F("Microphone init failed"));
    while (true) {
      delay(1000);
    }
  }

  USB_SERIAL.println(F("XIAO MG24 microphone streaming over USB CDC"));
  USB_SERIAL.println(F("Source: Seeed Arduino Mic DMA"));
  USB_SERIAL.print(F("Configuration - Sample Rate: "));
  USB_SERIAL.println(kSampleRateHz);
  USB_SERIAL.print(F("Configuration - Num Samples per block: "));
  USB_SERIAL.println(NUM_SAMPLES);
  USB_SERIAL.print(F("Configuration - ADC Midpoint: "));
  USB_SERIAL.println(kAdcMidpoint);
  USB_SERIAL.print(F("Configuration - ADC shift: "));
  USB_SERIAL.println(kAdcToPcmShift);
  USB_SERIAL.println(F("Setup complete. Awaiting audio samples..."));
}

void loop() {
  size_t samples = 0;

  const uint32_t nowMs = millis();
  if (nowMs - s_lastLoopEntryMs > 1000) {
    USB_SERIAL.print(F("Loop heartbeat - ms since last log: "));
    USB_SERIAL.println(nowMs - s_lastLoopEntryMs);
    USB_SERIAL.print(F("Callback count so far: "));
    USB_SERIAL.println(s_callbackCount);
    USB_SERIAL.print(F("Blocks streamed so far: "));
    USB_SERIAL.println(s_streamedBlocks);
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
    USB_SERIAL.println(F("onSamplesReady invoked with zero length buffer"));
    return;
  }

  size_t samples = min(static_cast<size_t>(length), NUM_SAMPLES);
  memcpy(s_captureBuffer, buffer, samples * sizeof(uint16_t));

  s_samplesCaptured = samples;
  s_samplesReady = true;
  ++s_callbackCount;

  USB_SERIAL.print(F("onSamplesReady - raw length: "));
  USB_SERIAL.println(length);
  USB_SERIAL.print(F("onSamplesReady - samples copied: "));
  USB_SERIAL.println(samples);
}

static void streamPcmBlock(const uint16_t *buffer, size_t samples) {
  if (samples == 0) {
    USB_SERIAL.println(F("streamPcmBlock invoked with zero samples"));
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
  USB_SERIAL.print(F("streamPcmBlock - block index: "));
  USB_SERIAL.println(s_streamedBlocks);
  USB_SERIAL.print(F("streamPcmBlock - bytes written: "));
  USB_SERIAL.println(bytesToWrite);
}

static void runSelfTests() {
  bool allPassed = true;

  if (testAdcConversionRange()) {
    USB_SERIAL.println(F("Self-test: ADC conversion range - PASS"));
  } else {
    USB_SERIAL.println(F("Self-test: ADC conversion range - FAIL"));
    allPassed = false;
  }

  if (testMicConfig()) {
    USB_SERIAL.println(F("Self-test: Microphone configuration - PASS"));
  } else {
    USB_SERIAL.println(F("Self-test: Microphone configuration - FAIL"));
    allPassed = false;
  }

  if (testBufferSizes()) {
    USB_SERIAL.println(F("Self-test: Buffer sizes - PASS"));
  } else {
    USB_SERIAL.println(F("Self-test: Buffer sizes - FAIL"));
    allPassed = false;
  }

  if (testPcmSignedRange()) {
    USB_SERIAL.println(F("Self-test: PCM signed range - PASS"));
  } else {
    USB_SERIAL.println(F("Self-test: PCM signed range - FAIL"));
    allPassed = false;
  }

  if (allPassed) {
    USB_SERIAL.println(F("All self-tests passed."));
  } else {
    USB_SERIAL.println(F("One or more self-tests failed. Streaming will continue with caution."));
  }
}

static bool testAdcConversionRange() {
  bool success = true;

  const int16_t minSample = convertAdcSampleToPcm(0);
  const int16_t midSample = convertAdcSampleToPcm(kAdcMidpoint);
  const int16_t maxSample = convertAdcSampleToPcm((1u << 12) - 1);

  USB_SERIAL.print(F("Test ADC Conversion - min raw 0 -> "));
  USB_SERIAL.println(minSample);
  USB_SERIAL.print(F("Test ADC Conversion - midpoint raw -> "));
  USB_SERIAL.println(midSample);
  USB_SERIAL.print(F("Test ADC Conversion - max raw -> "));
  USB_SERIAL.println(maxSample);

  if (midSample != 0) {
    USB_SERIAL.println(F("Expected midpoint to convert to 0"));
    success = false;
  }
  if (minSample >= 0) {
    USB_SERIAL.println(F("Expected min sample to be negative"));
    success = false;
  }
  if (maxSample <= 0) {
    USB_SERIAL.println(F("Expected max sample to be positive"));
    success = false;
  }

  return success;
}

static bool testMicConfig() {
  bool success = true;

  if (s_micConfig.channel_cnt != 1) {
    USB_SERIAL.print(F("Unexpected channel count: "));
    USB_SERIAL.println(s_micConfig.channel_cnt);
    success = false;
  }
  if (s_micConfig.sampling_rate != kSampleRateHz) {
    USB_SERIAL.print(F("Unexpected sampling rate: "));
    USB_SERIAL.println(s_micConfig.sampling_rate);
    success = false;
  }
  if (s_micConfig.buf_size != NUM_SAMPLES) {
    USB_SERIAL.print(F("Unexpected buffer size: "));
    USB_SERIAL.println(s_micConfig.buf_size);
    success = false;
  }

  USB_SERIAL.print(F("Mic config debug - channel count: "));
  USB_SERIAL.println(s_micConfig.channel_cnt);
  USB_SERIAL.print(F("Mic config debug - sampling rate: "));
  USB_SERIAL.println(s_micConfig.sampling_rate);
  USB_SERIAL.print(F("Mic config debug - buffer size: "));
  USB_SERIAL.println(s_micConfig.buf_size);
  USB_SERIAL.print(F("Mic config debug - debug pin: "));
  USB_SERIAL.println(s_micConfig.debug_pin);

  return success;
}

static bool testBufferSizes() {
  bool success = true;

  const size_t captureCount = sizeof(s_captureBuffer) / sizeof(s_captureBuffer[0]);
  const size_t processingCount = sizeof(s_processingBuffer) / sizeof(s_processingBuffer[0]);
  const size_t pcmCount = sizeof(s_pcmBuffer) / sizeof(s_pcmBuffer[0]);

  USB_SERIAL.print(F("Buffer size debug - capture count: "));
  USB_SERIAL.println(captureCount);
  USB_SERIAL.print(F("Buffer size debug - processing count: "));
  USB_SERIAL.println(processingCount);
  USB_SERIAL.print(F("Buffer size debug - pcm count: "));
  USB_SERIAL.println(pcmCount);

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

  USB_SERIAL.print(F("PCM range debug - min value: "));
  USB_SERIAL.println(minValue);
  USB_SERIAL.print(F("PCM range debug - max value: "));
  USB_SERIAL.println(maxValue);

  if (minValue < INT16_MIN) {
    USB_SERIAL.println(F("PCM minimum exceeded INT16_MIN"));
    success = false;
  }
  if (maxValue > INT16_MAX) {
    USB_SERIAL.println(F("PCM maximum exceeded INT16_MAX"));
    success = false;
  }

  if (minValue >= maxValue) {
    USB_SERIAL.println(F("PCM min should be less than max"));
    success = false;
  }

  return success;
}

static void logBufferStatistics(const uint16_t *buffer, size_t samples) {
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

  USB_SERIAL.print(F("Capture stats - min: "));
  USB_SERIAL.print(minValue);
  USB_SERIAL.print(F(", max: "));
  USB_SERIAL.print(maxValue);
  USB_SERIAL.print(F(", avg: "));
  USB_SERIAL.println(average);
}

static void logPcmStatistics(const int16_t *buffer, size_t samples) {
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

  USB_SERIAL.print(F("PCM stats - min: "));
  USB_SERIAL.print(minValue);
  USB_SERIAL.print(F(", max: "));
  USB_SERIAL.print(maxValue);
  USB_SERIAL.print(F(", avg: "));
  USB_SERIAL.println(average);
}

static int16_t convertAdcSampleToPcm(uint16_t sample) {
  int32_t value = static_cast<int32_t>(sample);
  value -= kAdcMidpoint;
  value <<= kAdcToPcmShift;
  return static_cast<int16_t>(value);
}
