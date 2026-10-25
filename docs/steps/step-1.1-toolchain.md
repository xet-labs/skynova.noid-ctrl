# Step 1.1: toolchain, repo, blink

**Goal (one line):** prove that PlatformIO + ESP-IDF build and flash this project, and that the board runs our code.

## The idea, in plain words
- **PlatformIO** is the build tool: it downloads the compiler and ESP-IDF, builds, flashes and opens the serial monitor. Its settings live in `firmware/platformio.ini`.
- **ESP-IDF** is Espressif's C framework. It starts FreeRTOS (a tiny scheduler) and then calls our `app_main()` once, in its own task.
- **Blink** is the smallest program that proves the whole chain: build, flash, run, and visible output.

```
 main.c --pio run--> firmware.bin --upload (USB, CP2102)--> ESP32
                                                              |
                                  LED on GPIO2 blinks 1 Hz <--+--> boot line on the serial monitor
```

`firmware/platformio.ini` has two environments: `single` (the board) and `native` (host tests, used from step 1.2).

## Files you will touch
`firmware/platformio.ini`, `firmware/src/main.c`, `firmware/CMakeLists.txt`, `firmware/src/CMakeLists.txt` (these two are the same as in your `esp32-idf_blinky` project), `.gitignore`.

## What you will see when it works
```
cd skynoid-v1-ctrl/firmware
pio run -e single -t upload
pio device monitor            (115200 baud)
  skynoid single boot, reset_reason=1      (1 = power-on; 4 = software reset)
```
and the on-board LED blinks once per second.

## After the first successful build (do these, then tell me)
1. Note the platform and ESP-IDF versions that `pio run` printed. Pin the platform in `platformio.ini` (`platform = espressif32@<version>`) and write the versions into `docs/measurements.md`. Later code differs between IDF 4.4 and 5.x (ADC, I2C, UART console calls).
2. `git init` this folder and commit: `step 1.1: toolchain and blink`. Add a private remote and push ([[05-working-agreement-code]] in the vault).
3. Core dump to flash (a `coredump` partition and `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`) is part of step 1.1 in the spec. It needs `partitions.csv` and `sdkconfig.defaults`, which I left for after your first build so they are written against the real IDF version.

## If it does not work (three likeliest causes)
1. Upload fails ("Failed to connect"): hold BOOT while it says "Connecting...", or the USB cable is power-only.
2. Nothing in the monitor: wrong baud rate (must be 115200 for now) or opening the monitor reset the board, so press EN once.
3. Build error about `IDF_PATH` or a CMake version: the CMake files here are copied from your working blinky project, so compare against it.

## Checkpoint
What calls `app_main()`, and why does the `while (1)` loop need `vTaskDelay` instead of a busy wait?

---
**Status note.** Written without running it: I cannot build or flash from here. The PlatformIO settings for the `lib/` folder and the `native` environment are what I expect to work and need your first run to confirm.
