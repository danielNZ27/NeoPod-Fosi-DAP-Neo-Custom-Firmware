# NeoPod

[![Release](https://img.shields.io/github/v/release/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware?color=teal&label=Latest%20Release)](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

**NeoPod** is an open-source custom firmware for the **Fosi Audio DAP Neo** digital audio player. It brings a clean, retro iPod-style interface, smooth navigation, album artwork support, Bluetooth audio with saved devices and automatic reconnect, and bit-perfect wired playback powered by a dual-core audio architecture.

**Production v1.8.0:** [Download the complete Windows/Python update package](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/raw/refs/heads/main/release/NeoPod-v1.8.0.zip) · [Release notes](release/RELEASE-v1.8.0.md). Applies the owner-confirmed wired-jack pull-bit fix automatically; diagnostic mode is off.

---

> [!CAUTION]
> **Use at your own risk.** Installing custom firmware carries inherent risks. The authors and contributors are not responsible for bricked devices, lost data, or hardware damage. This is an independent community project and is **not affiliated with or endorsed by Fosi Audio**.

---

## ✨ Features

- **Retro UI & Theming**: Classic iPod navigation layout, customizable accent colors (Teal, Purple, Green, Orange, Magenta), smooth Cover Flow view, and an authentic iPod Classic theme.
- **Dual-Core Audio Engine**: Bit-perfect wired decoding offloaded to CPU Core 1 for pop-free, hardware-accelerated playback.
- **Comprehensive Codec Support**: Native playback for FLAC (up to 24-bit / 192 kHz), MP3, AAC-LC, Ogg Opus, and uncompressed WAV.
- **Smart Bluetooth Stack**: Low-latency pairing, automatic reconnection, and storage for up to 6 saved devices with seamless PCM anti-pop transitions.
- **Advanced Sound Tuning**: 5-band custom EQ (-12 dB to +12 dB) with user presets, plus ReplayGain track/album modes with anti-clipping protection.
- **Fast Media Indexing**: Embedded ID3/Vorbis cover extraction and directory-level artwork scanning (`cover.jpg`, `folder.jpg`).
- **Headphone Auto Pause/Resume**: Wired unplug or Bluetooth loss pauses active music; reconnect resumes only an automatically paused session. Manual pause stays paused. Neo uses its one 3.5 mm output, with the confirmed GPIO0.13 pull bit 1 applied on boot.
- **Library Stats & Bluetooth Status**: Settings → System → Stats shows songs, artists, albums and total duration without a slow free-space scan. The top-bar Bluetooth icon flashes while disconnected and stays solid after a confirmed link.
- **Windows Updater**: A three-page GUI with live recovery indication, visible progress/logs and safe ejection, plus the supported Python command-line tool.

---

## 📸 Interface Preview

| Lock Screen | Home (Custom Accent) | Cover Flow |
| :---: | :---: | :---: |
| ![Lock screen](assets/screenshots/lock-teal.png) | ![Home screen](assets/screenshots/home-purple.png) | ![Cover Flow](assets/screenshots/cover-flow-blue.png) |

| Custom EQ | iPod Classic Theme | Bluetooth Manager |
| :---: | :---: | :---: |
| ![Custom EQ](assets/screenshots/custom-eq-magenta.png) | ![iPod Classic layout](assets/screenshots/ipod-home.png) | ![Bluetooth menu](assets/screenshots/bluetooth-main.png) |

---

## 📥 Installation

### Method 1: First-Time Install (From Stock Firmware)

1. Extract the [complete v1.8.0 package](release/NeoPod-v1.8.0.zip), including **`DP3_V180.IMG`** and **`sd-card/.echo/`**.
2. Connect your DAP Neo to your computer via USB-C and power it on.
3. Place **`DP3_V180.IMG`** directly in the root directory of the player's **Internal Storage**.
4. Copy the entire **`.echo`** directory to the root directory of your **MicroSD Card** (resulting in `[SD Card]:/.echo/neo_*.bin`).
5. Safely unmount and disconnect the USB cable.
6. The stock bootloader will automatically detect the image and flash NeoPod.
   > *Note: If the player does not reboot automatically after flashing, press the hardware reset pinhole next to the USB-C port once.*

---

### Method 2: Updating an Existing NeoPod Install (Direct Flashing)

You can flash new firmware images directly using the Rockchip bootloader interface without reverting to stock:

1. Extract the [complete v1.8.0 package](release/NeoPod-v1.8.0.zip). Before recovery, select USB Storage mode in the player's settings, then unplug USB and power off (hold **Power** for 5 seconds).
2. Hold **VOL +**, **VOL -**, and **Center / OK** simultaneously (leave **Power** untouched).
3. Connect the USB-C cable to your PC while holding the three buttons; release after 3–5 seconds. The screen will remain black in Rockusb recovery mode.
4. Open the included **flash-neo.exe**, choose **DP3_V180.IMG**, and follow its three pages. The recovery indicator turns green only for a live recovery connection; the app does not enter recovery automatically. The Python CLI remains supported from the extracted folder:
   ```powershell
   python flash-neo.py DP3_V180.IMG
   ```
5. Keep USB connected. Both tools validate the matching manifest, flash both slots, try normal reset (0) first, and copy/read-verify all six matching MicroSD companions. Existing settings, library, artwork and saved Bluetooth devices are preserved. No rescan is needed.
6. Select **Safely eject** in the GUI, or press Enter in the CLI at completion. If reboot stalls, press the pinhole reset once while the tool waits; no repeat flash is needed. For companion-only recovery, use the GUI's **Finish SD companions only** mode or CLI `--sync-only`.

See [all flash helper options and build instructions](docs/flash-neopod.md).

---

## 🎛️ Navigation & Controls

| Control | Function |
| :--- | :--- |
| **Scroll Wheel** | Navigate menus, scroll lists, and adjust EQ sliders |
| **Center / OK (Single Press)** | Confirm / Select / Play / Pause |
| **Center / OK (Double Press)** | Back / Return to parent screen |
| **VOL+ / VOL-** | Master volume control |
| **Power (Short Press)** | Screen wake / sleep |
| **Power (Long Press)** | System power off |

---

## 🎧 Audio Engine & Supported Formats

Decoding is handled across the Rockchip SoC to maximize battery efficiency and eliminate audio dropouts:

| Codec | Extensions | Specifications | Execution Core |
| :--- | :--- | :--- | :--- |
| **FLAC** | `.flac` | 16/24-bit up to 192 kHz, Vorbis comments, embedded art | Core 1 (`neo_flac.bin`) |
| **MP3** | `.mp3` | CBR & VBR up to 320 kbps, ID3v1/v2 tags, embedded art | Core 1 (`neo_mp3.bin`) |
| **AAC** | `.m4a` | AAC-LC containerized streams (Helix AAC) | Core 1 (`neo_aac.bin`) |
| **Opus** | `.opus` | Ogg Opus 48 kHz native playback | Core 1 (`neo_opus.bin`) |
| **WAV** | `.wav` | Linear PCM (16/24-bit RIFF/WAVE) | Core 0 (System Core) |

### MicroSD Companion Binaries (`.echo/`)
Ensure the following files remain in the root `.echo/` directory on your MicroSD card:
- `neo_flac.bin` / `neo_mp3.bin` / `neo_aac.bin` / `neo_opus.bin`: Secondary core audio decoders.
- `neo_scan.bin`: Background tag indexer and metadata parser.
- `boot.rgb565`: Custom boot animation asset.

---

## 🛠️ Building from Source

### Prerequisites
- CMake 3.25+
- Ninja build system
- ARM GNU Embedded Toolchain (`arm-none-eabi-gcc`)
- 64-bit Python 3.12 (tested), with the release build requirements below

### Compile
Run the build script to compile binaries and package the firmware image:

```powershell
python -m venv .venv-flasher
.\.venv-flasher\Scripts\python.exe -m pip install -r tools/requirements-release.txt
./build-neopod.ps1
```

The build writes `release/DP3_V180.IMG`, matching companions and manifests, both flash tools, a verified package in `build/NeoPod-v1.8.0/`, and `release/NeoPod-v1.8.0.zip`. Production explicitly disables jack diagnostic mode. Both updater formats are maintained from the same flashing engine.

---

## 🙏 Credits & Acknowledgments

NeoPod is an adapted hardware port of **[EchoPod](https://github.com/BasilSkyWalk/echopod)** by **BasilSkyWalk** and the EchoPod open-source community. Gratitude goes to the upstream team for the core interface, theme engine, and audio routing architecture. 

NeoPod adapts this system for the Fosi Audio DAP Neo's Rockchip platform, ST7789 portrait display interface, custom GPIO routing, and resistor key ladder.

---

## 🤖 AI Disclosure

This project is created with the assistance of AI tools. 

---

## 📄 License

This project is licensed under the [GNU General Public License v3.0](LICENSE) in accordance with the upstream EchoPod project.
