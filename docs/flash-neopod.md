# NeoPod Windows flash and companion updater

Both `flash-neo.py` and `flash-neo.exe` are supported. The EXE opens Firmware Studio, a graphical app for 64-bit Windows; no Python installation is required. It uses the Python helper as its flashing engine. Keep either helper with the complete firmware package. This update changes the PC helper; no firmware rebuild is required.

Double-click `flash-neo.exe` to open the three-step wizard:

1. **Firmware package:** select an image and update mode, then choose **Check package**. Firmware headers and matching companion checksums are verified before connection preparation.
2. **Connect your player:** use the displayed recovery-button instructions. A live indicator shows green **Connected in recovery mode** or red **Not connected in recovery mode**, updating every second. Only a confirmed recovery connection enables **Start update**. Companion-only mode instead asks you to confirm the SD card is mounted normally.
3. **Update & finish:** progress and the complete activity log stay visible without scrolling the page. Select **Safely eject** when the companion update completes. **Save log** exports diagnostics. Controls are locked during an operation.

The app asks before writing firmware. Step 2 only checks whether the recovery interface is present; it never requests recovery or restarts the player. No device operation starts merely by opening the app.

The mode menu also offers **Finish SD companions only** after a successful flash/manual reboot, and **Firmware only** for stock restoration. Leave the SD drive blank for automatic NeoPod identification, or enter an explicit non-system drive root such as `F:\`. Reset and SD detection can take several minutes; keep USB connected and follow the activity messages.

The Python CLI remains supported with all existing options: `python flash-neo.py DP3_V180.IMG`. GUI operations never start automatically on launch.

## Maintaining both formats

`NeoPod/flash-neo.py` is the shared flashing engine; `flash_neo_gui.py` provides the UI. Edit it and rebuild the EXE together; do not maintain a separate EXE implementation. Every firmware release must include both helpers, the GUI source, `FLASHER-SHA256SUMS.txt`, and the existing firmware/companion package. Helper hashes are separate from `SHA256SUMS.txt` so building the PC tool never rewrites firmware checksums.

One-time setup on Windows with 64-bit Python 3.12 (or another Python supported by PyInstaller):

```powershell
python -m venv .venv-flasher
.\.venv-flasher\Scripts\python.exe -m pip install -r tools/requirements-flasher.txt
```

Build or refresh a package from the NeoPod repository:

```powershell
.\.venv-flasher\Scripts\python.exe tools/build_flash_neo.py --output build/NeoPod-v1.8.0
```

Omit `--output` to update `release`. `build-neopod.ps1` also builds both helpers into `release`; pass `-FlasherPython PATH` to select another prepared build interpreter. The helper build runs backend and GUI mock tests plus real graphical EXE startup, screenshot and image-discovery checks without accessing USB. Firmware and companions remain external to the EXE so each package retains its matching version.

## Normal update

1. Set **Settings → System → USB mode → Storage** before entering recovery.
2. Enter recovery using Vol+ + Vol- + OK while plugging in USB, then release those buttons.
3. Run `python flash-neo.py build/NeoPod-v1.7.4/DP3_V174.IMG` from the repository, or `python flash-neo.py DP3_V174.IMG` from the build folder.
4. Keep USB connected during reset and the SD mount wait. If automatic reboot stalls, press the pinhole reset once; the script continues waiting without reflashing.
5. After the six companions are copied and verified, press Enter to request safe ejection of DAP Neo and both drives. Closing the window leaves it mounted. If Windows vetoes ejection, close open player files/Explorer windows and use Safely Remove Hardware before unplugging.

Without an image argument the helper selects the highest-numbered DP3 image in the repository release folder or alongside the helper. Its matching manifest must validate the image and all six companions before any firmware writes begin.

## Reset handling

The reset command packet follows Rockchip's implementation. The flow tries normal reset (subcode 0) first, then disconnect reset (4), and MSC boot (1) as the last fallback. The owner confirmed normal reset works while MSC boot does not on this Neo. Each mode uses live recovery-pipe checks for up to 12 seconds per attempt. Three consecutive absent probes are required before reporting that recovery disconnected. A reset response alone is never claimed as proof that firmware booted; the subsequent SD mount confirms that normal storage became available. Power-off and Maskrom reset modes are not used.

Reference: https://github.com/rockchip-linux/rkdeveloptool/blob/master/RKComm.cpp and RKComm.h.

## Companion update

The package layout is firmware image + `SHA256SUMS.txt` + `sd-card/.echo/`. The allowlist is `neo_scan.bin`, `neo_flac.bin`, `neo_mp3.bin`, `neo_aac.bin`, `neo_opus.bin`, and `boot.rgb565`. Both bare and package-relative checksum entries are accepted.

Automatic Windows detection requires the NeoPod USB ancestry (VID 1209, PID 0001), ECHOPOD SD-card product name, and LUN 1. Multiple matching volumes require an explicit choice. No generic removable drive or existing `.echo` folder authorizes automatic copying. Use `--sd-drive F:\` to explicitly select your MicroSD drive root if necessary; the system drive and subdirectories are rejected.

Files are staged, flushed, backed up and read-verified. Only the six companions are replaced. Existing companions remain backed up inside `.echo/.neopod-update-*/backup/`; settings, saved devices, database, artwork and unrelated files are untouched. A detected replacement failure triggers rollback, with backups retained if the card becomes unavailable. A mount timeout reports the incomplete companion update and does not write to another drive.

## Options

- `--no-eject-prompt`: finish without waiting for Enter; the player remains mounted.
- `--sync-only`: validate the specified package and finish the SD copy without opening recovery, flashing or resetting.
- `--skip-companions`: firmware-only update, including stock image restoration.
- `--mount-timeout 300`: wait up to five minutes for the SD card.
- `--companions-dir PATH`: use another companion directory, still validated against the image's manifest.
- `--reset-only`: try the revised reset sequence without writing firmware or copying companions.

If no SD appears, check Storage mode, use pinhole reset if needed, and retry the same image with `--sync-only --sd-drive F:\`. No repeat firmware flash is needed after a successful flash.

## Verification

Mock-only tests cover reset packets/modes/order, early USB detach, transient pipe disappearance, manifest mismatches before any USB access, delayed mount, ambiguous volumes, replacement rollback, preserved settings/library/devices, firmware-write failure, and the complete flash-to-copy order. Tests use temporary directories and mocked USB. Physical reboot behavior still needs confirmation on a Neo.

## Recovery connection indicator

Enter recovery manually using Vol+ + Vol− + OK while connecting USB with the player powered off. Step 2 shows a green dot and **Connected in recovery mode** only when the Neo recovery pipe can be opened. A missing, stale, ambiguous or inaccessible interface shows a red dot and **Not connected in recovery mode**. Normal USB storage does not count. Unplugging the player disables **Start update** on the next check; selecting Start update also refreshes the connection first.

All automatic recovery-entry code and the experimental CLI option have been removed. Post-flash reboot to normal mode still uses normal reset (0) first.
