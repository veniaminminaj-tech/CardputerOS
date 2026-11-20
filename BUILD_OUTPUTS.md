# Build outputs

This source tree defines the two firmware targets:
- `launcher` -> `.pio/build/launcher/firmware.bin`
- `cardputer_os` -> `.pio/build/cardputer_os/firmware.bin`

The GitHub Actions workflow at `.github/workflows/build.yml` builds both and uploads them as the `cardputeros-firmware` artifact. Download the artifact from the completed workflow run.

`gaming.cpp` is a separate sample application source for the Launcher's Gaming OTA slot. It is not one of the two firmware targets and must be built as its own application before it can be copied to `/boot/gaming.bin`.

Important: these files have not been compiled in this environment because PlatformIO and the ESP32 toolchain are not installed and package downloads are unavailable. Treat the first CI build as a compile check. Verify the EXT connector pin mapping against your exact Cardputer ADV revision before connecting the ILI9341.
