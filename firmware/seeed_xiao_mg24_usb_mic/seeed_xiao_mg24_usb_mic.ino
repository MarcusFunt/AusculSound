#include <SilabsMicrophoneAnalog.h>

// Microphone wiring for the Seeed Studio XIAO MG24 Sense.
// PC9 is the analog output of the onboard MEMS microphone and PC8 provides its
// power rail.
constexpr uint8_t kMicDataPin = PC9;
constexpr uint8_t kMicPowerPin = PC8;

// Audio configuration. Increase NUM_SAMPLES to trade latency for throughput.
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

// DMA buffer owned by the microphone driver and a local copy used for
// processing while the next block is captured.
static uint32_t s_dmaBuffer[NUM_SAMPLES];
static uint32_t s_localBuffer[NUM_SAMPLES];
static int16_t s_pcmBuffer[NUM_SAMPLES];

static volatile bool s_samplesReady = false;
static MicrophoneAnalog s_mic(kMicDataPin, kMicPowerPin);

static void onSamplesReady();
static void streamPcmBlock();

void setup() {
  USB_SERIAL.begin(115200);
  uint32_t start = millis();
  while (!USB_SERIAL && (millis() - start < 5000)) {
    delay(10);
  }

  // Prepare the microphone driver and begin sampling immediately.
  s_mic.begin(s_dmaBuffer, NUM_SAMPLES);
  s_mic.startSampling(onSamplesReady);

  USB_SERIAL.println(F("XIAO MG24 microphone streaming over USB CDC"));
  USB_SERIAL.println(F("Source: SilabsMicrophoneAnalog DMA"));
}

void loop() {
  if (!s_samplesReady) {
    return;
  }

  noInterrupts();
  bool ready = s_samplesReady;
  s_samplesReady = false;
  interrupts();

  if (ready) {
    streamPcmBlock();
  }
}

static void onSamplesReady() {
  memcpy(s_localBuffer, s_dmaBuffer, sizeof(s_dmaBuffer));
  s_samplesReady = true;
}

static void streamPcmBlock() {
  // Halt sampling while we convert the freshly captured block to PCM.
  s_mic.stopSampling();

  for (size_t i = 0; i < NUM_SAMPLES; ++i) {
    int32_t sample = static_cast<int32_t>(s_localBuffer[i]);
    sample -= kAdcMidpoint;
    sample <<= kAdcToPcmShift;
    s_pcmBuffer[i] = static_cast<int16_t>(sample);
  }

  const size_t bytesToWrite = NUM_SAMPLES * sizeof(s_pcmBuffer[0]);
  USB_SERIAL.write(reinterpret_cast<uint8_t *>(s_pcmBuffer), bytesToWrite);

  // Resume capturing the next block.
  s_mic.startSampling(onSamplesReady);
}
