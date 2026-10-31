# Changelog

## v1.7.0 — 2026-10-31

Firmware image: `DP3_V170.IMG`. [Release notes and upgrade instructions](release/RELEASE-v1.7.0.md).

- Added working Bluetooth audio for the Neo, with Settings → Audio → Bluetooth, an On/Off toggle, explicit device discovery/pairing and up to six saved devices. Bluetooth defaults to Off and remembers the chosen setting.
- Added saved-device Connect, Disconnect and confirmed Forget actions. Automatic reconnect uses saved addresses only, pauses in Pair new device and never falls back to the new-pair command. Device discovery and matching names do not authorize pairing.
- Start audio automatically after an approved connection settles; preserve the connection when leaving the menu. Persist the device list and preferred device in a checksummed `.echo/bluetooth.bin` file.
- Follow the Neo's stock GPIO2.1 audio route while preserving its DAC-enable and OK-button pins. Handle stock uppercase/lowercase connected reports and explicit disconnect statuses.
- Fixed the reported Bluetooth play/pause/skip pop: PCM fade-out holds a silent tail, a dedicated DMA loop feeds zeros during pauses and same-rate track loading, and resume rejoins the music ring at the paused stereo frame with a fade-in. Keep the external DAC's mute/volume state steady during these transitions and settle initial DAC setup before module unmute.
- Added a saved UART diagnostic trace at `.echo/neo-bt.txt` after a disconnect/off and regression tests for explicit pairing, reconnect policy, status handling, PCM transitions and DMA resume.
- The owner confirmed the exact V170 image plays correctly and resolves the reported popping on the physical Neo/headphone setup. Other headphones and sample-rate transitions are not comprehensively tested.

**Upgrade from v1.6.2:** flash `DP3_V170.IMG` and replace `.echo/neo_scan.bin`. Other decoder and boot files are unchanged. Preserve settings, saved devices, library and artwork. Downloads are individual files.

## v1.6.2 — 2026-10-31

Firmware image: `DP3_V162.IMG`. See [release notes](release/RELEASE-v1.6.2.md) for details and upgrade instructions.

- Added a separate **iPod Classic** theme under Settings → Display & Controls → Theme, with silver headers, white menus, blue highlights and a full-height home menu.
- Added dark classic Now Playing with larger 184 × 184 artwork and blue progress, retaining NeoPod's playback, volume, shuffle/repeat and seek controls. Theme selection persists; the original theme and button colour remain available.
- Replaced the text boot splash with full-screen 240 × 320 artwork loaded from `/.echo/boot.rgb565`, fading from black using the existing strip buffer. Missing or invalid artwork falls back to text. Added the artwork converter and packaging step.
- Improved Cover Flow rendering speed during motion and capped animation time steps to prevent slow frames from snapping through transitions; smoothing returns at rest. Preserved readable text in the light theme.
- Corrected header alignment in both themes: titles down two pixels; playback/battery/charging/percentage indicators down one pixel from their original positions.
- Fixed the eight-label/twelve-row browser cache mismatch. Folders, All Songs, Albums and Artists now populate all twelve visible rows.
- Added **Song name - Artist** labels to All Songs, with horizontal scrolling for long selected entries after the existing pause.
- Corrected device version reporting to v1.6.2, updated the image version/build date, and expanded the code allocation within the stock module limits.
- Added regression coverage for the new UI behaviour and refreshed the cover fixture to the current artwork dimensions.

**Upgrade:** flash `DP3_V162.IMG` and merge the matching `sd-card/.echo` files, including `boot.rgb565` and `neo_scan.bin`, into the SD card. Preserve existing settings, library and covers; no rescan is required from v1.6.1.

## v1.6.1 — 2026-10-31

Firmware image: DP3_V161.IMG. This release brings high-quality album artwork scaling with real-time bilinear interpolation, Floyd-Steinberg dithering, and a 5-category Settings menu.

### High Quality Album Artwork Upgrade
- Upgraded stored album art resolution from 112×112 to **128×128** (+31% pixel resolution) with **64×64** half-size thumbnails.
- Added real-time hardware-optimized bilinear interpolation (color_bilinear) in quad and column scaling. Album art on the Home Menu (128×128 → 170×170), Cover Flow center (128×128 → 150×150), and perspective side quads appear smooth and naturally filtered without blocky nearest-neighbor pixelation.
- Fixed blue line artifacts: isolated and blended 5-bit red, 6-bit green, and 5-bit blue channels independently in color_lerp with rounding (+128u) before packing into RGB565.
- Added 2D Floyd-Steinberg error-diffusion dithering for cover export, eliminating 16-bit RGB565 gradient banding.
- Fixed Now Playing layout: explicitly set PORTRAIT_COVER_SIZE = 112 downsampled from 128, preserving clean 6px spacing above the song title.
- Preserved rescan stability with static BSS allocation and 14.8 KB of free headroom in internal SRAM.

### Categorized Settings Submenus
- Replaced the single 15-item flat list with 5 categorized submenus:
  1. **Playback:** Shuffle, Repeat, ReplayGain
  2. **Audio:** Equalizer, Gain, Output (3.5 mm / 4.4 mm)
  3. **Display & Controls:** Brightness, Screen timeout, Button colour, Side buttons, Start screen
  4. **Power & Sleep:** Sleep timer, Auto off
  5. **System:** USB mode, Rescan library
- Full native ScreenStack integration with chevrons and instant Menu/Back button popping.

## v1.5.2 — 2026-10-31

Firmware image: `DP3_V152.IMG`. This release collects the changes since v1.4.7,
including the V151 FLAC fix and V152 Custom EQ controls confirmed on a DAP Neo.

### Fixed: scratchy FLAC playback with very short blocks

- Fixed a decoder handoff bottleneck exposed by a 24-bit / 44.1 kHz FLAC using
  **47-sample blocks**. Previously each block occupied one of two PCM slots,
  leaving only about **2.13 ms** queued between the decoder and playback engine.
- Short blocks now accumulate in the existing PCM slots before publication,
  targeting at least 2,048 samples per slot. For 47-sample blocks, each slot holds
  2,068 samples; two slots provide approximately **93.8 ms** of queued PCM.
- Input bytes are released after each decoded block so the compressed-data ring
  can refill while an output batch is still being assembled.
- Final partial batches are delivered before the end/error marker. Seeking drops
  unpublished batches and starts the new stream with fresh batch state.
- Added checks for output capacity and contiguous sample positions while batching.
- Uses the existing PCM slot allocation; no additional PCM buffers are needed.
- Keeps the 32 KiB SD refill budget and ring-filling playback pump introduced
  after v1.4.7.
- Avoids unnecessary 64-bit gain arithmetic at unity gain and repeated checks of
  flat/disabled EQ for every output sample pair.
- Retains hardware LPC restoration for larger FLAC blocks using the accelerator's
  existing split-DMA implementation, and optimized software LPC restoration.

The reported full-length track decoded sample-for-sample identically to FFmpeg,
both directly and through streaming with two input-ring sizes. The owner then
confirmed the updated firmware eliminated the scratchiness on the actual Neo.
This fix does not require converting or changing the original music file.

### Custom EQ designed for the Neo

- Replaced Echo Mini's fixed landscape slider coordinates with a layout derived
  from screen dimensions. All five bands fit the Neo's **240 × 320** display.
- Portrait layout shows every frequency, slider and signed gain value, together
  with the selected band's full frequency/gain heading and separate control hints.
- Turn the **wheel** to select a band; selection wraps through all five bands.
- Press **OK** to enter adjustment mode, turn the wheel to raise/lower gain,
  then press **OK** again to confirm and save.
- Wheel events now reach the Custom EQ controls directly. Physical volume buttons
  continue to control playback volume through Neo's existing global handling.
- Gain changes apply live while EQ is enabled and stay within **−12 to +12 dB**.
- The existing Back gesture leaves the screen; confirming or leaving saves settings.

### Equalizer and ReplayGain

- Added a fixed-point five-band EQ: **100 Hz, 300 Hz, 1 kHz, 3 kHz, 10 kHz**.
- Added built-in presets, custom gain editing and up to **eight named user presets**,
  with save, load, overwrite and delete controls.
- Added ReplayGain **Off / Track / Album** settings, tag probing across the supported
  audio sources, peak-based anti-clipping and gain transitions between tracks.
- Added persistent EQ/ReplayGain settings, backward decoding of legacy settings,
  validation of gain values and stored presets, and hardware application on load.
- Added output/DAC diagnostic state for playback, volume, DSP and underrun inspection.

### AAC-LC, Opus and library support

- Added dedicated Core 1 AAC and Opus programs, selected on demand during playback.
- Integrated the Helix fixed-point AAC decoder and MP4/M4A probing/seeking support.
- Integrated fixed-point libopus with Ogg packet handling, pre-skip trimming,
  duration/seeking support and continuation after input starvation.
- Added `AAC` and `OPUS` format indicators to Now Playing and format plumbing in
  the player/HAL.
- Added `.opus` library recognition, Vorbis/Opus tag handling and embedded Opus
  cover extraction, including reads that move backward within an Ogg stream.
- Extended packaging/build targets for `neo_aac.bin` and `neo_opus.bin` alongside
  the existing FLAC, MP3 and scanner companions.
- Added decoder source/license credits for EchoPod Enhanced, Helix AAC and libopus.

### Verification and developer tools

- Added synthetic high-bitrate 24-bit / 44.1 kHz and 47-sample-block FLAC fixtures;
  no commercial music is included in the repository or release.
- Added tests for tiny-block batching, two queued batches, tail handling, seeking,
  input-ring wraparound and sample-exact output.
- Added accelerator tests covering full 4,096/4,608-sample blocks, EQ output tests,
  wheel wraparound, gain bounds, live DSP changes and confirmation saving.
- Added a host FLAC decode probe and optional full-file streaming comparison.
- Rendered and visually checked portrait EQ selection and adjustment states.
- Device build, image header/CRC checks and companion decoder CRC checks pass.
- Host FLAC, streaming, accelerator, EQ, ReplayGain, settings and Custom EQ suites
  pass. The full Neo-configured host run passes **109 of 117** suites. The eight
  failures are `test_toast`, `test_app`, `test_test_pattern_screen`, `test_dac_sequence`,
  `test_key_ladder`, `test_panel`, `test_audio_engine` and `no_comments`. The full
  suite is not claimed fully passing; see the release notes for details.

### Installation and upgrade requirements

Download the complete release ZIP, or the firmware and all five companion files:

| File | Destination / purpose |
| --- | --- |
| `DP3_V152.IMG` | Firmware image, installed using the existing Neo update procedure |
| `neo_flac.bin` | MicroSD `/.echo/`; includes the tiny-block playback fix |
| `neo_mp3.bin` | MicroSD `/.echo/`; MP3 decoder |
| `neo_aac.bin` | MicroSD `/.echo/`; AAC decoder |
| `neo_opus.bin` | MicroSD `/.echo/`; Opus decoder |
| `neo_scan.bin` | MicroSD `/.echo/`; library scanner and metadata/artwork |

- **From v1.4.7 or earlier:** install the firmware and replace/add all five matching
  companion files. Updating only the firmware does not install the FLAC fix.
- **From the tested V151 package:** update the firmware image and `neo_scan.bin`.
  The four audio decoder binaries are unchanged from V151, but the scanner overlay
  changed with the V152 build and must match the firmware.
- Merge the `.echo` folder. Preserve your existing settings, library, covers and
  other data; do not delete the folder to upgrade.
- Decoder programs continue loading from microSD. This release does not enable
  the unverified internal-storage decoder loader.
- EQ and ReplayGain intentionally process audio when enabled; sample-exact neutral
  playback checks do not imply bit-perfect output with those effects active.

## v1.4.7

Previous public NeoPod release. Includes the Neo hardware port, portrait interface,
wheel/OK navigation, FLAC/MP3/WAV playback, artwork/library support and Rockusb
update/recovery tooling. See its GitHub release and repository history for details.
