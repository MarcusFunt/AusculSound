"""Stream audio from the XIAO MG24 microphone over USB CDC.

The firmware uses the Seeed Arduino Mic DMA driver and streams 16-bit PCM
blocks. This helper script connects to the enumerated serial port and plays the
incoming audio via the system's default audio output.
"""

import time

import numpy as np
import serial
import sounddevice as sd

# Serial port where the XIAO MG24 shows up.
# On Windows it might be "COM3", on Linux "/dev/ttyACM0" or "/dev/ttyUSB0"
SERIAL_PORT = "COM8"  # <-- change this to match your system
BAUDRATE = 921600
SAMPLE_RATE = 16000
CHUNK_SAMPLES = 256  # Must match NUM_SAMPLES in the firmware sketch.

# Open serial connection and flush any banner text from the firmware.
ser = serial.Serial(SERIAL_PORT, BAUDRATE)
time.sleep(0.5)
ser.reset_input_buffer()

# Callback to continuously stream audio
def audio_callback(outdata, frames, time, status):
    if status:
        print(status)
    outdata.fill(0)
    # Each sample is 2 bytes (little-endian signed PCM from the firmware)
    expected_bytes = CHUNK_SAMPLES * 2
    raw = ser.read(expected_bytes)
    if len(raw) != expected_bytes:
        return
    # Convert to numpy array of signed 16-bit samples
    samples = np.frombuffer(raw, dtype=np.int16)
    outdata[:len(samples), 0] = samples.astype(np.float32) / 32768.0  # normalize to [-1, 1]

# Open an audio output stream
with sd.OutputStream(
    samplerate=SAMPLE_RATE,
    channels=1,
    dtype="float32",
    callback=audio_callback,
    blocksize=CHUNK_SAMPLES,
):
    print("Streaming audio from XIAO MG24... Press Ctrl+C to stop.")
    try:
        while True:
            sd.sleep(1000)
    except KeyboardInterrupt:
        print("Stopped.")
