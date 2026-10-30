# NeoPod v1.5.2 — FLAC playback fix and portrait Custom EQ

Fixes scratchy playback of FLAC files with very short blocks and makes Custom EQ
fit the Fosi Audio DAP Neo's 240×320 screen with wheel/OK controls. Both fixes have
been confirmed working by the device owner. This release also publishes the
AAC-LC, Opus and ReplayGain work accumulated since v1.4.7.

## Install / upgrade

Download **NeoPod-v1.5.2.zip** for the firmware, all five companion files, notices
and installation notes, or download the individual assets below.

1. Install **DP3_V152.IMG** using your existing NeoPod firmware update procedure.
   When using the stock updater, copy it to the DAP SYS/internal-storage root.
2. Copy the ZIP's **sd-card** contents to the microSD root, merging the **.echo**
   folder. Keep your existing settings, library, cover cache and other files.
3. For an upgrade from **v1.4.7 or earlier**, replace/add **all five** companions:
   `neo_flac.bin`, `neo_mp3.bin`, `neo_aac.bin`, `neo_opus.bin`, `neo_scan.bin`.
   The FLAC fix requires the new `neo_flac.bin`; the firmware image alone is not enough.
4. If you already installed the complete **V151** package, update **DP3_V152.IMG**
   and the matching **neo_scan.bin**. The four audio decoders are unchanged from V151,
   but the scanner overlay has changed. Copying all five matching files is also fine.

`SHA256SUMS.txt` provides checksums for the downloadable firmware, companions and ZIP.

## Detailed changes

### FLAC playback

- Fixed tiny-frame handoff starvation. A reported 24-bit / 44.1 kHz file used
  47-sample blocks, leaving only about 2 ms of PCM queued in the two handoff slots.
- Aggregate small frames into the existing output buffers. The same file now has
  roughly 94 ms queued, without allocating another PCM buffer.
- Keep compressed-input refills moving while batches accumulate, preserve the
  final partial batch and reset unpublished batches on seek.
- Retain the 32 KiB SD refill budget, ring-filling output pump, optimized software
  LPC restoration and split-DMA hardware acceleration for larger FLAC blocks.
- Skip unnecessary unity-gain arithmetic and flat/disabled EQ work.

### Equalizer and controls

- Five-band fixed-point EQ: 100 Hz, 300 Hz, 1 kHz, 3 kHz, 10 kHz; −12 to +12 dB.
- Built-in presets plus up to eight named user presets, with save/load/delete controls.
- Custom EQ now fits all five sliders, frequency labels and gain values on the Neo.
- **Wheel:** choose a band, wrapping through all five.
- **OK:** select it for adjustment; **wheel:** raise/lower gain; **OK:** confirm/save.
- Changes apply live with EQ enabled. Physical volume keys retain playback volume control.

### Formats, tags and audio settings

- Dedicated Core 1 AAC-LC and Opus decoders (`neo_aac.bin`, `neo_opus.bin`).
- Helix AAC decoding with supported MP4/M4A probing and seeking.
- Fixed-point libopus, Ogg packet handling, pre-skip trimming, duration/seeking and
  recovery from input starvation.
- AAC/OPUS Now Playing indicators, `.opus` library discovery and Opus metadata/artwork extraction.
- ReplayGain Off / Track / Album, peak-based anti-clipping and gain transitions.
- Persistent EQ/ReplayGain settings and named presets, with legacy-settings migration.
- Additional output/DAC/DSP/underrun diagnostics and regression-test tools.
- Updated attribution for EchoPod Enhanced, Helix AAC and libopus; license texts
  and notices are included in the complete download and source tree.

## Validation and known test limitations

- Full reported FLAC matches FFmpeg's decoded samples directly and through the
  updated streaming path. On-device scratchiness is confirmed resolved.
- On-device EQ operation and portrait wheel/OK controls are confirmed working.
- Synthetic tests cover high-bitrate 24-bit audio, 47-sample frames, batching,
  final partial batches, seeks, ring wraps, gain bounds and settings persistence.
- Device build, firmware header/CRC and decoder CRC checks pass.
- **Full host run: 109 of 117 CTest suites pass.** The eight failing suites are
  `test_toast`, `test_app`, `test_test_pattern_screen`, `test_dac_sequence`,
  `test_key_ladder`, `test_panel`, `test_audio_engine` and `no_comments`.
  Several retain Echo Mini hardware/UI expectations; the audio-engine cases
  expect one frame per pump rather than filling available ring space, and
  `no_comments` rejects source comments. These results are disclosed; the full
  suite is not claimed passing. The FLAC, streaming, accelerator, codec, EQ,
  ReplayGain, settings and Custom EQ suites pass.
- Optional checks unavailable in this host setup (formatting, target audit and
  tools-venv checks) are not counted as passing. The ARM device build was checked separately.
- EQ and ReplayGain alter audio when enabled. Neutral-path sample comparisons do
  not imply bit-perfect output with audio effects active.
- Decoder programs still load from the microSD card. No internal-storage decoder
  loader or new flashing mechanism is enabled.

See [CHANGELOG.md](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/blob/main/CHANGELOG.md)
for implementation details and the repository history for source changes.
