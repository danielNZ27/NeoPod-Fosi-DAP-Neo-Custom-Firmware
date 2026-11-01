# NeoPod v1.7.2 — Bluetooth Connection Status Fix

Firmware: **DP3_V172.IMG** for the Fosi Audio DAP Neo.

## Changes

- Reconnect acknowledgements no longer immediately count as Connected. NeoPod queries the link and waits for a complete CQO/CQo connected status report before displaying Connected, making the Bluetooth icon solid, or starting Bluetooth audio.
- Keep status polling and the existing reconnect timeout while waiting for real confirmation. Powered-off headphones should no longer cause false Connected/disconnected cycling from acknowledgements alone; real connections still enable audio normally.
- Removed free storage from Stats. **Settings → System → Stats** retains total songs, artists, albums, and total music hours/minutes.
- Retains Bluetooth pairing, saved devices, reconnect, smooth playback transitions, and the hidden/flashing/solid top-bar Bluetooth icon.

## Install

Flash **DP3_V172.IMG** and replace **`.echo/neo_scan.bin`** on the MicroSD card with the matching file from `sd-card/.echo`. Preserve settings, saved Bluetooth devices, library and artwork. No rescan is required. Codec binaries and boot artwork are unchanged.

`RKNANOFW.IMG` is an identical firmware copy for recovery tools. For a first installation, copy the complete `sd-card/.echo` folder to the MicroSD card root.

## Validation

ARM release build and firmware bounds/header/CRC checks pass. Six focused host suites pass, covering module protocol, Bluetooth UI/manager, Stats/audio settings, status-bar icon, PCM transitions, and DMA resume. Regression cases include repeated reconnect acknowledgements plus disconnected reports, connection timeout, late/short link notices, and successful connection when a real connected report arrives. Stats was rendered and checked in both themes. Hardware confirmation of this fix is pending.
