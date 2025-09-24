import serial
import numpy as np
import sounddevice as sd

# Serial port where the XIAO MG24 shows up.
# On Windows it might be "COM3", on Linux "/dev/ttyACM0" or "/dev/ttyUSB0"
SERIAL_PORT = "COM8"    # <-- change this to match your system
BAUDRATE = 921600
SAMPLE_RATE = 8000
CHUNK_SAMPLES = 256     # must match firmware's SAMPLES_PER_TRANSFER

# Open serial connection
ser = serial.Serial(SERIAL_PORT, BAUDRATE)

# Callback to continuously stream audio
def audio_callback(outdata, frames, time, status):
    if status:
        print(status)
    # Each sample is 2 bytes (uint16 little-endian)
    expected_bytes = CHUNK_SAMPLES * 2
    raw = ser.read(expected_bytes)
    # Convert to numpy array
    samples = np.frombuffer(raw, dtype=np.uint16)
    # The firmware sends 12-bit values (0–4095) in a 16-bit word.
    # Convert to signed 16-bit PCM centered at 0.
    samples = samples.astype(np.int32) - 2048
    samples = (samples << 4).astype(np.int16)  # scale 12-bit to 16-bit
    outdata[:len(samples), 0] = samples / 32768.0  # normalize to [-1, 1]

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
