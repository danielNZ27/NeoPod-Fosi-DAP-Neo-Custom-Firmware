# NeoPod v1.7.0 — Bluetooth audio and smooth playback transitions

Firmware image: **DP3_V170.IMG** for the **Fosi Audio DAP Neo**.
This release publishes the exact V170 image confirmed working by the owner.

## Bluetooth

- **Settings → Audio → Bluetooth**, with Bluetooth Off by default and a saved
  On/Off preference.
- **Pair new device** lists nearby headphones/speakers. Only clicking a device
  requests pairing; background reconnect pauses while browsing this menu.
- Up to **six saved devices**, with Connect, Disconnect and confirmed Forget.
- Automatic reconnect uses previously saved devices only. Explicit Disconnect
  stays disconnected until Connect or a Bluetooth Off/On toggle.
- Audio starts automatically after an approved connection settles, and Back
  preserves the connection.

## Fixed: Bluetooth popping on play, pause and next track

- PCM fades end in sustained silence instead of jumping back to full level.
- A dedicated silent DMA loop keeps the module's I2S feed running during pause
  and same-rate track loading. Resume rejoins at the paused stereo frame.
- Hold the external DAC's mute/volume controls steady during Bluetooth playback
  transitions, and settle DAC setup before enabling module audio.

The owner confirmed this build resolves the reported popping on the physical
Neo/headphone setup. Other headphones and mixed sample-rate transitions have
not been comprehensively tested. [Bluetooth guide](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/blob/main/docs/bluetooth.md).

## Upgrade from v1.6.2

1. Download **DP3_V170.IMG** and **neo_scan.bin** from this release.
2. Replace **`.echo/neo_scan.bin`** on the MicroSD card.
3. Flash **DP3_V170.IMG** using the existing NeoPod update procedure.
4. Open Settings → Audio → Bluetooth to enable Bluetooth and pair a device.

Other codec companions and `boot.rgb565` are byte-for-byte unchanged from
v1.6.2. Preserve settings, `bluetooth.bin`, library and artwork. No manual library
rescan is required for these changes.

For a first installation or an older setup, download all five `neo_*.bin` files
and `boot.rgb565` and put them in **`.echo`** at the MicroSD card root. Firmware
goes to the player via its normal update/flash procedure, not inside `.echo`.
Downloads are individual files; no ZIP extraction is required.

The repository also includes the existing Windows flashing helper:
`python flash-neo.py release/DP3_V170.IMG` after entering Rockusb recovery.
See the README for the device's recovery/update instructions.

## Saved devices and diagnostics

Saved records are checksummed in `.echo/bluetooth.bin`. Forget removes NeoPod's
saved record and disables automatic reconnect for it; it does not claim to erase
the module's internal bond keys or the headphones' own record. A recent UART
trace can be saved to `.echo/neo-bt.txt` after a disconnect/off for bug reports.

## Validation

- Five focused test suites pass: Bluetooth UI/manager/persistence/reconnect,
  module protocol, PCM transitions, DMA ring/resume and audio settings.
- ARM firmware/scanner/codec builds pass. Image checks verify stock-template
  SHA-256, module/RAM bounds, duplicated header and CRC-32.
- The release firmware is byte-for-byte the owner-tested V170 image.
- The wider legacy Neo host suite has nine pre-existing failures, independently
  reproduced from unchanged v1.6.2 sources. A clean full test suite is not claimed.

**Full comparison:** [V1.6.2…V1.7.0](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/compare/V1.6.2...V1.7.0)
