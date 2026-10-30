# NeoPod

**NeoPod** is an open-source custom firmware for the **Fosi Audio DAP Neo** digital audio player. It brings a clean, retro iPod-style interface, smooth navigation, album artwork support, and bit-perfect playback powered by a dual-core audio architecture.

---

## 🙏 Credits

NeoPod is a direct hardware port of **[EchoPod](https://github.com/BasilSkyWalk/echopod)** by **BasilSkyWalk** and the EchoPod open-source contributors. 

Huge credit and gratitude go to the original EchoPod developers for creating the user interface, audio engine, theme framework, and core system architecture. NeoPod adapts this foundation specifically for the Fosi Audio DAP Neo's Rockchip SoC, ST7789 portrait display bus, custom GPIO/DAC routing, and key ladder.

---

## ⚠️ Disclaimer

> [!IMPORTANT]
> **Use at your own risk.** The author and contributors of this project are **NOT responsible** for any device damage, data loss, bricked hardware, or hardware failure that may result from installing or using this software.
> 
> This is an independent, community-developed open-source project. The author is **NOT affiliated with, endorsed by, or connected to Fosi Audio in ANY WAY.**

---

## 🎵 Supported Audio Formats & Codecs

NeoPod features hardware-accelerated playback offloaded to the secondary processor core (Core 1):

| Format | File Extension | Specifications & Features | Decoding Engine |
| :--- | :--- | :--- | :--- |
| **FLAC** | `.flac` | Free Lossless Audio Codec (16-bit / 24-bit, up to 192 kHz bit-perfect lossless), Vorbis Comments, embedded artwork | Hardware Core 1 (`neo_flac.bin`) |
| **MP3** | `.mp3` | MPEG Audio Layer III (CBR & VBR up to 320 kbps), ID3v1 / ID3v2.3 / ID3v2.4 tags, embedded artwork | Hardware Core 1 (`neo_mp3.bin`) |
| **WAV** | `.wav` | Uncompressed Linear PCM (16-bit / 24-bit RIFF/WAVE) | System Core |

### Artwork & Metadata Support
- **Embedded Artwork:** Automatically extracts embedded JPEG and PNG front album covers from FLAC and MP3 files.
- **Folder Artwork:** Reads external image files in the album folder (e.g. `cover.jpg`, `folder.jpg`, `cover.png`).
- **Music Library Tagging:** Fast library scanning and cataloging by Artist, Album, Track, and Title handled by the background indexer (`neo_scan.bin`).

---

## 🚀 Installation Guide (First Time from Stock Firmware)

Follow these steps if your DAP Neo is currently running the factory stock firmware:

### 1. Download Release Files
From the [`release/`](./release/) folder in this repository, obtain:
- `DP3_V147.IMG` (The NeoPod firmware image)
- The `sd-card/.echo` folder (containing `neo_flac.bin`, `neo_mp3.bin`, and `neo_scan.bin`)

### 2. Copy Files to the DAP Neo
1. Power on your DAP Neo normally and connect it to your PC using a USB-C cable.
2. Two drives will mount on your computer:
   - The **Internal Storage** drive
   - The **MicroSD Card** drive
3. Copy **`DP3_V147.IMG`** directly to the **root** of the **Internal Storage** drive (`/`).
4. Copy the entire **`.echo`** folder directly to the **root** of your **MicroSD Card** (`/`).
   *(The `.echo` folder must be in the root directory so the path is `[SD Card]:\.echo\neo_*.bin`)*.

### 3. Apply the Firmware
1. Safely eject and unplug the USB-C cable from the DAP Neo.
2. The player's stock bootloader will detect `DP3_V147.IMG` and begin the flashing process.
3. Once completed, the player will automatically reboot directly into **NeoPod**!

---

## 🔄 Updating NeoPod (Crucial!)

> [!WARNING]
> **If you already have NeoPod installed and want to install a newer release of NeoPod, you MUST first revert to the stock factory firmware.**
>
> NeoPod does not run the background stock firmware update daemon. If you try to update from within NeoPod, the update image will not be detected.
>
> **Required Update Procedure:**
> 1. Use the **[Fosi DAP Neo Firmware Restore Tool](https://github.com/danielNZ27/Fosi-Dap-Neo-firmware-restore-tool)** to restore your player back to stock firmware via Rockusb mode.
> 2. Once your DAP Neo boots back into the stock factory OS, follow the standard [Installation Guide](#-installation-guide-first-time-from-stock-firmware) to flash the new NeoPod version.

---

## 🎮 Controls & Navigation

| Control | Action |
| :--- | :--- |
| **Scroll Wheel / Vol+ / Vol-** | Scroll through menus and lists |
| **Center / OK (Single Press)** | Select / Play / Enter menu |
| **Center / OK (Double Tap)** | Back / Return to previous screen |
| **Power Button (Short Press)** | Screen sleep / wake |
| **Power Button (Long Hold)** | Power off (Goodbye screen) |

---

## 🛠️ Companion Files (`.echo` on MicroSD)

NeoPod utilizes the DAP Neo's secondary CPU core (Core 1) for real-time audio decoding and background database indexing:
- `neo_flac.bin`: Dedicated hardware FLAC audio decoder for Core 1.
- `neo_mp3.bin`: Dedicated hardware MP3 audio decoder for Core 1.
- `neo_scan.bin`: Ultra-fast library rescan and metadata engine.

*Always keep the `.echo` directory on your MicroSD card updated with the files matching your firmware release.*

---

## 💻 Building from Source

### Requirements
- **CMake** 3.20+
- **Ninja** build system
- **ARM GNU Toolchain** (`arm-none-eabi-gcc`)
- **Python** 3.8+

### Build Script
To compile NeoPod and package the firmware image automatically:
```powershell
./build-neopod.ps1
```
The compiled firmware image and companion binaries will be generated in `build/echopod-neo/`.

---

## 📄 License

NeoPod is licensed under the **GNU General Public License v3.0** (GPL-3.0), inheriting the open-source license of the upstream EchoPod project. See [LICENSE](./LICENSE) for details.
