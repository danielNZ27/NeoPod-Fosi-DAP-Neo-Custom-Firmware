# NeoPod v1.7.1 — Library Stats and Bluetooth Status

Firmware: **DP3_V171.IMG** for the Fosi Audio DAP Neo.

## New

- **Settings → System → Stats**: total songs, artists, albums, free MicroSD storage, and total indexed music duration in hours and minutes.
- Stats refresh on opening. Storage and duration calculations run in small steps so the page stays responsive. Free space is shown in GiB/MiB; values show Calculating while pending or Unavailable on a read error. Library totals reflect the last library scan.
- Bluetooth symbol in the top bar in both themes: hidden when Off, flashing every half second while On and disconnected, and solid when connected, including paused playback.
- Includes v1.7.0 Bluetooth pairing, saved devices, automatic reconnect and playback-transition improvements.

## Upgrade

Flash **DP3_V171.IMG** and replace **`.echo/neo_scan.bin`** on the MicroSD card with this build's matching file. Preserve settings, `.echo/bluetooth.bin`, library and artwork. The codec binaries and boot artwork remain unchanged from v1.7.0. No library rescan is required unless your music has changed.

For a first installation, copy all files from `sd-card/.echo` into `.echo` at the MicroSD card root. `RKNANOFW.IMG` is an identical firmware copy for recovery tools; the normal update file is `DP3_V171.IMG`.

## Validation

ARM release build and firmware image bounds/header/CRC validation pass. Focused host tests cover statistics/navigation, pending and failed storage, 64-bit duration, FAT16/FAT32 allocation counting, exFAT bitmap counting and Bluetooth icon visibility/blinking, alongside existing Bluetooth and playback-transition tests. Production Stats screens were rendered and checked in both themes. This new build has not yet been tested on physical hardware.
