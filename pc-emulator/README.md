# KolibriX PC Emulator

Windows desktop simulator for the KolibriX / CardputerOS interface.

## Features
- Cardputer-style 320x240 UI, scaled to a resizable PC window.
- Demo login: root / root.
- Desktop icons, keyboard and mouse input.
- Terminal with a small set of demo commands.
- Files app backed by the local pc-emulator/sdcard folder.
- Settings and simulated device information.

## Limitations
This is not an ESP32 emulator and does not execute firmware.bin. It simulates the desktop UI and PC-side input/storage. Wi-Fi, battery, GPIO, SD electrical behavior and M5Cardputer APIs are not emulated.

## Run on Windows
1. Install Python 3.10 or newer.
2. From the repository root run:
   python -m pip install -r pc-emulator/requirements.txt
3. Run:
   python pc-emulator/kolibrix_emulator.py

The emulator creates pc-emulator/sdcard on first start. Put test files there.

## Controls
- Login: root, Enter, root, Enter.
- Desktop: arrows or WASD, Enter to open, mouse click to open.
- Terminal: Ctrl+Alt+T, then type help.
- Close app: Esc or Ctrl+W.
- Files: arrows/WASD, Enter to open, Backspace to go up or return from a file.
- Settings: arrows/WASD, A/D to adjust brightness, Enter to activate.
- Alt+Tab: cycle desktop selection.

## Windows EXE
The GitHub Actions workflow .github/workflows/pc-emulator.yml builds a Windows executable and uploads it as an artifact.
