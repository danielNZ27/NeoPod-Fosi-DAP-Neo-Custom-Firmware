# NeoPod v1.7.3 — Headphone Auto Pause and Resume

Firmware: **DP3_V173.IMG** for the Fosi Audio DAP Neo.

- Unplugging wired headphones pauses music if it was playing. Plugging headphones back in resumes that automatically paused session.
- Losing a confirmed Bluetooth connection pauses active music. Reconnection resumes the session once the connection is confirmed and Bluetooth audio is ready.
- Manually paused music stays paused on connection. Manual play/pause cancels pending automatic resume; no automatic-resume intent is persisted through restart or output release.
- Wired jack changes do not interrupt connected Bluetooth playback. Direct switching between wired jacks keeps playing.
- Includes v1.7.2 Bluetooth status confirmation, Stats (songs/artists/albums/music duration), and the Bluetooth status icon.

## Install

Flash **DP3_V173.IMG** and replace **`.echo/neo_scan.bin`** on the MicroSD card from `sd-card/.echo`. Preserve existing settings, saved Bluetooth devices, library and artwork. No rescan is required. For a first installation, copy all supplied `.echo` files to the MicroSD card root.

`RKNANOFW.IMG` is an identical firmware copy for recovery tools.

## Validation

Seven focused host suites pass: player, Bluetooth module, Bluetooth UI/manager, audio settings/Stats, status-bar icon, PCM transitions and DMA resume. Tests cover wired unplug/replug, Bluetooth loss/reconnect with delayed stream readiness, manual-pause protection and wired changes during Bluetooth playback. ARM release build and firmware image bounds/header/CRC validation pass. Physical-device testing is pending.
