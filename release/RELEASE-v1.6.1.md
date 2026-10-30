# NeoPod v1.6.1 — High Quality Album Artwork & Categorized Settings

Firmware image: `DP3_V161.IMG`. This release brings major visual and navigation improvements to the Fosi Audio DAP Neo: high-resolution artwork scaling with real-time bilinear interpolation, Floyd-Steinberg error-diffusion dithering, and a clean 5-category Settings menu structure.

## What is New in v1.6.1

### 🎨 High Quality Album Artwork Upgrade
- **Upgraded Artwork Resolution:** Stored covers upgraded from 112×112 to **128×128** (+31% pixel resolution) with **64×64** half-size thumbnails for Cover Flow.
- **Hardware-Optimized Real-Time Bilinear Filtering:** Replaced nearest-neighbor point sampling with real-time bilinear interpolation (`color_bilinear`) inside image quad scaling. Scaled artwork on the Home Menu (128×128 → 170×170), Cover Flow center (128×128 → 150×150), and perspective side quads appear smooth, naturally filtered, and free of blocky pixelation.
- **Color Accuracy & Blue Line Fix:** Arithmetic in RGB565 color interpolation (`color_lerp`) now isolates and blends 5-bit red, 6-bit green, and 5-bit blue channels independently with midpoint rounding (`+128u`), eliminating cross-channel arithmetic bit spillage and blue line artifacts.
- **Floyd-Steinberg Error-Diffusion Dithering:** Replaced raw 16-bit RGB565 bit truncation in cover export with 2D Floyd-Steinberg dithering, eliminating gradient banding and posterization.
- **Now Playing Spacing:** Portrait Now Playing screen explicitly constrains the album cover to `PORTRAIT_COVER_SIZE = 112`, preserving a clean 6-pixel gap between the cover bottom and the song title label without overlapping.
- **Rescan Stability:** Statically allocated rescan and cover buffers within `.bss` with **14.8 KB of free headroom** in internal SRAM, preventing bus faults during on-device library rescans.

### ⚙️ Categorized Settings Submenus
Replaced the single 15-item flat list with 5 organized categories/submenus:
1. **Playback:**
   - Shuffle (`Off`, `Songs`, `Albums`)
   - Repeat (`Off`, `One`, `All`)
   - ReplayGain (`Off`, `Track`, `Album`)
2. **Audio:**
   - Equalizer (Presets + 5-band custom EQ)
   - Gain (`Low`, `High`)
   - Output (`Auto`, `3.5 mm`, `4.4 mm`)
3. **Display & Controls:**
   - Brightness (`1`..`10`)
   - Screen timeout (`10 s`..`Never`)
   - Button colour (`Blue`..`Silver` theme accents)
   - Side buttons (`Volume up/down`, `Previous/next track`, `Hybrid`)
   - Start screen (`Main menu`, `Now Playing`, `Albums`, `Artists`, `Favorites`, `Playlists`, `Cover Flow`)
4. **Power & Sleep:**
   - Sleep timer (`Off`..`120 min`)
   - Auto off (`Off`..`10 min`)
5. **System:**
   - USB mode (`Storage`, `DAC`)
   - Rescan library

Native `ScreenStack` integration allows smooth forward navigation with chevrons and instantaneous Menu/Back button popping.

---

## Install / Upgrade Guide

Download **`NeoPod-v1.6.1.zip`** for the complete firmware image, companion decoders, notices, and installation tools, or download the individual assets below.

### Method A: Direct Flashing via Rockusb Mode (Recommended)
1. Turn off the DAP Neo (hold **Power** for 3–5 seconds until the screen turns off).
2. Press and hold **VOL +**, **VOL -**, and **Center / OK** simultaneously (do not touch Power).
3. While continuing to hold those 3 buttons, plug the USB-C cable into your PC. Keep holding for 3–5 seconds, then release. The screen remains black in recovery mode.
4. Run the flashing script:
   ```cmd
   python flash-neo.py release/DP3_V161.IMG
   ```
5. Copy [`release/sd-card/.echo/`](./release/sd-card/.echo/) to the root of your MicroSD card (specifically updating `neo_scan.bin`).
6. Boot up and enjoy!

### Method B: From Stock Bootloader
1. Copy **`DP3_V161.IMG`** directly to the root of the DAP Neo's Internal Storage drive.
2. Copy the **`.echo`** folder to the root of your MicroSD card.
3. Safely eject and unplug the USB-C cable; the player will update and reboot automatically.

---

## File Checksums (SHA-256)

```text
e207df1513ae0248f9bb22d5e1d049a0b7b9e3e2bb83cbc0aa68c99b09063468  DP3_V161.IMG
580d395a456da038262c210342142a27376608a174e21a1d795d45cf83b470c3  NeoPod-v1.6.1.zip
bf6d5edc23c8e1e50e3147d2c357bca5ac76738748a0349cef41bb1f056a66e3  neo_aac.bin
29650dc63ced74f71b6d730dd266e5a0652f30c9d1246dee2b7a74693a992d3f  neo_flac.bin
3304542a0c90c6066d71c8d4040354a293f4a190d39e1ac4f53c35ac8658be65  neo_mp3.bin
e09569acf0e7237eb6b17e4efa3a9e22fa2d5d66cf0e6a37b71beae8ca30c6ed  neo_opus.bin
fc0a944fc9ef943a6d6823d295898fb68b1d4de91af06d74a717f781316d4b7a  neo_scan.bin
```
