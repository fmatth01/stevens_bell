# nucleos

PlatformIO project for all STM32 Nucleo firmware in the Steven's Bell modular synthesizer.

## Project structure

```
nucleos/
├── platformio.ini      # Build environments for each module
├── src/
│   ├── sm/             # State machine module
│   ├── dsp/            # DSP / audio synthesis module
│   └── shared/         # Code shared across modules
├── include/            # Shared header files
├── lib/                # Local libraries
└── test/               # Unit tests
```

## Building and flashing

Open this `nucleos/` folder (not the repo root) in VS Code so the PlatformIO extension picks up `platformio.ini` correctly.

Each module has its own build environment defined in `platformio.ini`. To build and flash a specific module, use the PlatformIO toolbar in VS Code or run from the terminal:

```bash
# Build a module
pio run -e sm
pio run -e dsp

# Build and flash to a connected Nucleo
pio run -e sm -t upload
pio run -e dsp -t upload

# Clean build artifacts
pio run -t clean
```

To build all environments at once: `pio run`
