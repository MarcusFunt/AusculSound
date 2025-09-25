# AusculSound

This repository contains a simple Arduino sketch for the Seeed Studio XIAO MG24 Sense that streams the on-board analog microphone over USB CDC to a connected PC. The sketch targets the "Seeed Studio MG24 Boards" Arduino core and uses Silicon Labs' `SilabsMicrophoneAnalog` driver for reliable DMA-based sampling.

## Firmware overview

The sketch captures 12-bit samples from the microphone connected to pin `PC9` using a DMA buffer managed by `SilabsMicrophoneAnalog`. Each block of 256 samples is converted to signed 16-bit PCM and transmitted over the native USB CDC interface. Any serial terminal or host application that can read raw bytes from the virtual COM port can capture the audio stream.

### Key configuration values

- `SAMPLE_RATE_HZ` controls the audio sampling rate (default 16 kHz).
- `SAMPLES_PER_TRANSFER` adjusts the USB packet size (default 256 samples).

These constants can be tuned in the sketch to match the bandwidth and latency needs of your project.

## Building and uploading

1. Install the **Seeed Studio MG24 Boards** core in the Arduino IDE.
2. Add the **SilabsMicrophoneAnalog** library via the Arduino Library Manager (or copy it into your `libraries` folder).
3. Select **Seeed Studio XIAO MG24** (or XIAO MG24 Sense) as the target board.
4. Open the sketch located at `firmware/seeed_xiao_mg24_usb_mic/seeed_xiao_mg24_usb_mic.ino`.
5. Compile and upload the sketch to the board using the Arduino IDE.
6. Open a serial monitor (or capture program) on the enumerated USB COM port to receive the raw PCM stream.

## Host-side capture

For quick testing on a PC, you can use Python's `pyserial` to record the stream:

```python
import serial
import array

port = serial.Serial('COMx', 115200)
num_samples = 16000  # 1 second at 16 kHz
pcm = array.array('H', port.read(num_samples * 2))
```

Replace `COMx` with the actual port name (e.g., `/dev/ttyACM0` on Linux).
