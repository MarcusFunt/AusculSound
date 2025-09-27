# Seeed Arduino Mic (trimmed for AusculSound)

This repository vendors a minimal subset of the original
[Seeed_Arduino_Mic](https://github.com/Seeed-Studio/Seeed_Arduino_Mic)
library. Only the components required to build and test the XIAO MG24 USB
microphone firmware are retained.

The following upstream features have been removed to keep the dependency
surface small and avoid carrying unused examples or board support code:

- Wio Terminal DMA microphone driver
- nRF52840 PDM microphone driver
- Audio processing helpers and WAV utilities
- Example sketches, screenshots, and related assets

If you need the full feature set please refer to the upstream repository.
