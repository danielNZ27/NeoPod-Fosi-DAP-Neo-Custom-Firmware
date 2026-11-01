# NeoPod v1.7.4 — Wired Headphone Detection Fix

Firmware: **DP3_V174.IMG** for the Fosi Audio DAP Neo.

## Fixed wired auto pause/resume

The Neo adapter incorrectly supplied an active-low balanced-jack signal as active even though that detect input is unused on this board. It therefore reported headphones present permanently. The adapter now holds the unused signal inactive and follows the real GPIO0.13 wired detect pin, with the existing debounce. GPIO2.8 remains DAC mute and is not reconfigured or sampled as a jack input.

Unplugging wired headphones during playback should now pause music, and replugging resumes the automatically paused session. Music paused manually stays paused. The working Bluetooth connection/disconnection behavior is unchanged.

## Updated flash helper

From this folder run:

```powershell
python flash-neo.py DP3_V174.IMG
```

Set USB mode to Storage before entering recovery. The helper flashes, resets, waits for the NeoPod SD card, and backs up/copies/verifies all six matching `.echo` companion files. Existing settings, saved devices and library data are preserved.

At completion, **press Enter to safely eject DAP Neo**, or close the window to leave the player mounted. If Windows refuses ejection, the script reports that and asks you to close open player files and use Windows Safely Remove Hardware. You can use `--no-eject-prompt` for unattended completion.

If manual reset is needed, press the pinhole reset once while the script waits. To finish only the companion copy, use `python flash-neo.py DP3_V174.IMG --sync-only` after the SD drive mounts. Add `--sd-drive E:\` if automatic identification is unavailable, replacing E with your actual MicroSD drive letter.

## Validation

ARM release build and firmware bounds/header/CRC checks pass. Nine focused firmware suites pass, including an actual-board-adapter regression test for debounced wired insertion/removal without touching DAC mute, plus player/Bluetooth/Stats/icon/PCM/DMA tests. The flash helper passes 26 mock/temporary-file tests, including safe-eject targeting, cancelled prompts and Windows veto handling. This wired fix and eject prompt still require physical-device confirmation.
