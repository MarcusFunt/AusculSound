#include <Arduino.h>

// Analog microphone pin for the onboard MEMS microphone.
// The Seeed XIAO MG24 routes the microphone output to PC9.
constexpr uint8_t kMicAnalogPin = PC9;

// Audio configuration. Adjust SAMPLE_RATE_HZ to match the bandwidth you need.
constexpr uint32_t SAMPLE_RATE_HZ = 16000;
constexpr size_t SAMPLES_PER_TRANSFER = 256;

// The ADC on the MG24 is 12 bits wide. Centre the waveform around 0 and scale
// it up to fill a signed 16-bit sample for transmission over USB.
constexpr int16_t kAdcMidpoint = 1 << 11;           // 2048
constexpr uint8_t kAdcToPcmShift = 16 - 12;         // Shift left by 4 bits

// Derived constants.
constexpr uint32_t kSamplePeriodUs = 1000000UL / SAMPLE_RATE_HZ;

// Helper macro to pick the correct USB CDC serial port symbol provided by the
// Seeed Studio MG24 Arduino core.
#if defined(USBCDC_SERIAL_PORT)
#define USB_SERIAL USBCDC_SERIAL_PORT
#else
#define USB_SERIAL Serial
#endif

void setup() {
  // Initialise the USB CDC interface. The baud rate argument is ignored for
  // native USB devices but is kept for compatibility with serial monitors.
  USB_SERIAL.begin(115200);
  uint32_t start = millis();
  while (!USB_SERIAL && (millis() - start < 5000)) {
    delay(10);
  }

  // Configure the ADC resolution to the native 12 bits of the MG24 and set the
  // microphone pin as an input.
  analogReadResolution(12);
  pinMode(kMicAnalogPin, INPUT);

  USB_SERIAL.println(F("XIAO MG24 microphone streaming over USB CDC"));
  USB_SERIAL.print(F("Sample rate: "));
  USB_SERIAL.print(SAMPLE_RATE_HZ);
  USB_SERIAL.println(F(" Hz"));
}

void loop() {
  static int16_t buffer[SAMPLES_PER_TRANSFER];
  static size_t writeIndex = 0;
  static uint32_t nextSampleTimeUs = micros();

  uint32_t now = micros();
  if ((int32_t)(now - nextSampleTimeUs) >= 0) {
    nextSampleTimeUs += kSamplePeriodUs;

    // Read the raw 12-bit microphone value (0-4095). The result is stored as a
    // 16-bit word to simplify transmission as little-endian PCM samples.
    int32_t sample = static_cast<int32_t>(analogRead(kMicAnalogPin));
    sample -= kAdcMidpoint;
    sample <<= kAdcToPcmShift;
    buffer[writeIndex++] = static_cast<int16_t>(sample);

    if (writeIndex >= SAMPLES_PER_TRANSFER) {
      const size_t bytesToWrite = SAMPLES_PER_TRANSFER * sizeof(buffer[0]);
      USB_SERIAL.write(reinterpret_cast<uint8_t *>(buffer), bytesToWrite);
      writeIndex = 0;
    }
  }
}
