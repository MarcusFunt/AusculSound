#include <mic.h>

#include <cstring>

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

static mic_config_t s_micConfig{
    .channel_cnt = 1,
    .sampling_rate = kSampleRateHz,
    .buf_size = NUM_SAMPLES,
    .debug_pin = 0,
};

static MG24_ADC_Class s_mic(&s_micConfig);

static void onSamplesReady(uint16_t *buffer, uint32_t length);
static void streamPcmBlock(const uint16_t *buffer, size_t samples);

void setup() {
  USB_SERIAL.begin(115200);
  uint32_t start = millis();
  while (!USB_SERIAL && (millis() - start < 5000)) {
    delay(10);
  }

  s_mic.set_callback(onSamplesReady);
  if (!s_mic.begin()) {
    USB_SERIAL.println(F("Microphone init failed"));
    while (true) {
      delay(1000);
    }
  }

  USB_SERIAL.println(F("XIAO MG24 microphone streaming over USB CDC"));
  USB_SERIAL.println(F("Source: Seeed Arduino Mic DMA"));
}

void loop() {
  size_t samples = 0;

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
    return;
  }

  size_t samples = min(static_cast<size_t>(length), NUM_SAMPLES);
  memcpy(s_captureBuffer, buffer, samples * sizeof(uint16_t));

  s_samplesCaptured = samples;
  s_samplesReady = true;
}

static void streamPcmBlock(const uint16_t *buffer, size_t samples) {
  for (size_t i = 0; i < samples; ++i) {
    int32_t sample = static_cast<int32_t>(buffer[i]);
    sample -= kAdcMidpoint;
    sample <<= kAdcToPcmShift;
    s_pcmBuffer[i] = static_cast<int16_t>(sample);
  }

  const size_t bytesToWrite = samples * sizeof(s_pcmBuffer[0]);
  USB_SERIAL.write(reinterpret_cast<uint8_t *>(s_pcmBuffer), bytesToWrite);
}
