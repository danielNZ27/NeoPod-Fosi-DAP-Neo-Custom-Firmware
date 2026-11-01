# NeoPod v1.7.5 — Wired Jack Stabilization and 3.5 mm Only

Firmware: **DP3_V175.IMG** for the Fosi Audio DAP Neo.

## Changes

- Configure GPIO0.13 like the stock Neo code, with its internal pull disabled.
- Use time-based wired debounce: removal must remain stable for 150 ms; insertion must remain stable for 1.2 seconds. Short ghost insertions are ignored, preventing immediate resume from short noisy pulses after unplugging.
- Settings → Audio → Output now offers only Auto and 3.5 mm on Neo. Old balanced-output settings migrate to 3.5 mm; the hardware volume profile is fixed to the real 3.5 mm output.
- Diagnostics shows Jack 3.5 (raw pin) and Headphones (debounced Plugged in/Unplugged) instead of a nonexistent 4.4 mm jack.
- Existing Bluetooth connection/disconnection auto pause/resume is unchanged. Manually paused music remains paused.

## Install

From this folder run:

```powershell
python flash-neo.py DP3_V175.IMG
```

Use USB Storage mode before entering recovery. Keep USB connected while the helper flashes, resets, waits for the MicroSD drive, and copies/verifies all six matching companions. Settings, library, artwork and Bluetooth devices are preserved. Press Enter at completion to request safe ejection, or close the window to leave the drives mounted.

No rescan is required. This build changes the wired input configuration and filter, so physical testing is required: unplug during music and check it stays paused, then replug and allow about 1.2 seconds for stable insertion. If cycling continues, compare the Diagnostics → Jack 3.5 raw value with Headphones while unplugged and plugged in; those readings will distinguish persistent electrical activity from event handling.

## Validation

Ten focused firmware suites pass, covering the actual Neo board adapter, timed jack detection with noisy pulses and clock wrap, legacy output settings and two-choice UI, diagnostics, player, Bluetooth, status bar, PCM transitions and DMA. ARM release build and firmware bounds/header/CRC validation pass. Synthetic GPIO tests do not confirm real electrical behavior.
