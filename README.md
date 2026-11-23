# NFC Command Deck

ESP32-C3 and Windows NFC automation deck for RFID actions and desktop control.

## Summary

NFC Command Deck connects an ESP32-C3 microcontroller equipped with an RC522 RFID reader to a Windows desktop. Swiping registered RFID/NFC cards or tags triggers automated desktop actions such as launching applications, media controls, keystroke macros, and web requests. It includes an embedded firmware image, an interactive Studio GUI, and a background daemon service.

## Features

- Fast RFID tag detection and UID transmission over USB serial or Bluetooth LE.
- Windows Studio application for managing tag UID assignments and actions.
- Windows Background Daemon service that runs quietly in the system tray.
- Customizable action triggers including URL launch, shell commands, keyboard shortcuts, and sound effects.
- Web configuration captive portal on the ESP32-C3 for Wi-Fi and network preferences.

## Hardware Requirements

- Microcontroller: ESP32-C3 SuperMini development board
- RFID Reader: MFRC522 (RC522) 13.56MHz RFID / NFC module
- Status Indicator: Built-in or external LED
- Connecting wires and micro-USB/Type-C cable

## Pinout Table

| Module Pin (RC522) | ESP32-C3 SuperMini Pin | Function |
| --- | --- | --- |
| SDA (SS) | GPIO 7 | SPI Chip Select |
| SCK | GPIO 4 | SPI Clock |
| MOSI | GPIO 6 | SPI Data In |
| MISO | GPIO 5 | SPI Data Out |
| RST | GPIO 3 | Module Reset |
| 3.3V | 3.3V | Power Supply |
| GND | GND | Ground |
| Status LED | GPIO 8 | Activity Indicator |

## Secrets Configuration

1. In the `firmware/src/` folder, copy `secrets.example.h` to `secrets.h`.
2. Configure your network SSID, password, and portal security credentials inside `secrets.h`.
3. `secrets.h` is excluded in `.gitignore` to prevent credential exposure.
4. In the `app/` folder, `nfc_actions.json` holds your local mapped commands and is also kept local via `.gitignore`. A sample structure is available in `nfc_actions.example.json`.

## Build and Flash Steps

### Firmware (PlatformIO)

1. Open a terminal in the `firmware` directory:
   ```bash
   cd firmware
   ```
2. Build the firmware binary using PlatformIO:
   ```bash
   pio run
   ```
3. Connect your ESP32-C3 board and flash:
   ```bash
   pio run --target upload
   ```

### Windows Application (MinGW C++)

1. Open a terminal in the `app` directory:
   ```cmd
   cd app
   build.bat
   ```
2. The build script produces two binaries:
   - `NFC_Command_Deck.exe`: The interactive configuration and test GUI.
   - `NFC_Service.exe`: The background service for handling tags in real time.
