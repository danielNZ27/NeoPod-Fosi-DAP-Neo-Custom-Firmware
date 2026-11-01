# NeoPod flash and companion updater (Windows and macOS)

> **⚠️ macOS support is Alpha.** The macOS USB engine, SD detection, safe ejection and `.app` build are implemented and covered by automated mock tests, but the maintainer has no Mac and has **not** validated a real flash on macOS hardware. **Testing is greatly appreciated** — please report success or failure (Mac model, macOS version, player firmware, and the saved log) in a GitHub issue or discussion. Windows remains the supported production path.

Both `flash-neo.py` and the packaged app are supported. The Windows `flash-neo.exe` and the macOS `flash-neo.app` open Firmware Studio, a graphical app that requires no Python installation. Both use the Python helper as their flashing engine. Keep either helper with the complete firmware package. This update changes the PC helper; no firmware rebuild is required.

Double-click the app (or run `python flash-neo.py` / `python3 flash-neo.py`) to open the three-step wizard:

1. **Firmware package:** select an image and update mode, then choose **Check package**. Firmware headers and matching companion checksums are verified before connection preparation.
2. **Connect your player:** use the displayed recovery-button instructions. A live indicator shows green **Connected in recovery mode** or red **Not connected in recovery mode**, updating every second. Only a confirmed recovery connection enables **Start update**. Companion-only mode instead asks you to confirm the SD card is mounted normally.
3. **Update & finish:** progress and the complete activity log stay visible without scrolling the page. Select **Safely eject** when the companion update completes. **Save log** exports diagnostics. Controls are locked during an operation.

The app asks before writing firmware. Step 2 only checks whether the recovery interface is present; it never requests recovery or restarts the player. No device operation starts merely by opening the app.

The mode menu also offers **Finish SD companions only** after a successful flash/manual reboot, and **Firmware only** for stock restoration. Leave the SD drive blank for automatic NeoPod identification, or enter an explicit non-system drive root such as `F:\` on Windows or `/Volumes/NeoPod` on macOS. Reset and SD detection can take several minutes; keep USB connected and follow the activity messages.

The Python CLI remains supported with all existing options: `python flash-neo.py DP3_V182.IMG` on Windows, or `python3 flash-neo.py DP3_V182.IMG` on macOS. GUI operations never start automatically on launch.

## Maintaining both formats and platforms

`NeoPod/flash-neo.py` is the shared flashing engine; `flash_neo_gui.py` provides the UI. Edit it and rebuild each platform's app together; do not maintain a separate app implementation. The Windows app talks to Rockusb through WinUSB, while the macOS app talks to the same Rockchip vendor interface through libusb-1.0. Every firmware release must include both helpers, the GUI source, `FLASHER-SHA256SUMS.txt`, and the existing firmware/companion package. Helper hashes are separate from `SHA256SUMS.txt` so building the PC tool never rewrites firmware checksums. macOS helper hashes live in `FLASHER-SHA256SUMS-macos.txt`.

One-time setup on Windows with 64-bit Python 3.12 (or another Python supported by PyInstaller):

```powershell
python -m venv .venv-flasher
.\.venv-flasher\Scripts\python.exe -m pip install -r tools/requirements-flasher.txt
```

Build or refresh a package from the NeoPod repository:

```powershell
.\.venv-flasher\Scripts\python.exe tools/build_flash_neo.py --output build/NeoPod-v1.8.2
```

Omit `--output` to update `release`. `build-neopod.ps1` also builds both helpers into `release`; pass `-FlasherPython PATH` to select another prepared build interpreter. The helper build runs backend and GUI mock tests plus real graphical EXE startup, screenshot and image-discovery checks without accessing USB. Firmware and companions remain external to the EXE so each package retains its matching version.

### macOS

> **⚠️ Alpha:** macOS support is new and untested on physical hardware by the maintainer, who does not own a Mac. The build, parsers, and USB/ejection logic have automated test coverage only. **Please share your test results** (Mac model, macOS version, and the flasher log) in a GitHub issue so this can move out of alpha.

**No Mac? Let GitHub build the app for your tester.** The `Build macOS flasher` workflow compiles both architectures, runs the tests, and uploads ready-to-test packages. Prebuilt alpha downloads are published at [v1.8.2-macos-alpha.1](https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/releases/tag/v1.8.2-macos-alpha.1); to build a new one:

1. Open the repository's **Actions → Build macOS flasher → Run workflow** (or push a change to the flasher files).
2. When the run finishes, download the artifact for the tester's Mac:
   - `flash-neo-macos-apple-silicon` for M1/M2/M3/M4 Macs.
   - `flash-neo-macos-intel` for Intel Macs.
3. Send the tester `NeoPod-macos-…-ready-to-test.zip`. It already contains `flash-neo.app`, the firmware image, `SHA256SUMS.txt`, and `sd-card/.echo/` side by side.
4. On the Mac: unzip, Control-click `flash-neo.app` → **Open** (the app is unsigned), then follow the three pages.

To build locally instead, PyInstaller cannot cross-compile a `.app`, so run this on macOS (Apple Silicon or Intel). Install libusb once; the build embeds the dylib in the app bundle so end users do not need Homebrew:

```bash
brew install libusb python@3.12
python3 -m venv .venv-flasher
.venv-flasher/bin/python -m pip install -r tools/requirements-flasher.txt
.venv-flasher/bin/python tools/build_flash_neo_macos.py --output build/NeoPod-macos
```

The macOS build runs the shared and macOS-specific mock tests, smoke-tests the frozen app offscreen, then writes `flash-neo.app/`, `flash-neo-macos.zip`, the helper sources, and `FLASHER-SHA256SUMS-macos.txt` to `--output` (default `release/`). Place firmware images, `SHA256SUMS.txt`, and `sd-card/` beside `flash-neo.app`, not inside it. The app looks for firmware in the folder containing the bundle.

The app is unsigned, so the first launch may be blocked by Gatekeeper. Control-click the app and choose **Open**, or allow it under **System Settings → Privacy & Security**. If macOS still refuses, run `xattr -dr com.apple.quarantine /path/to/flash-neo.app` once. The CLI needs libusb from Homebrew (`brew install libusb`); the packaged app already embeds it. To use a different dylib, set `NEOPOD_LIBUSB` to its path.

## Normal update

1. Set **Settings → System → USB mode → Storage** before entering recovery.
2. Enter recovery using Vol+ + Vol- + OK while plugging in USB, then release those buttons.
3. Run `python flash-neo.py build/NeoPod-v1.7.4/DP3_V174.IMG` from the repository, or `python flash-neo.py DP3_V174.IMG` from the build folder. On macOS use `python3`, or open `flash-neo.app`.
4. Keep USB connected during reset and the SD mount wait. If automatic reboot stalls, press the pinhole reset once; the script continues waiting without reflashing.
5. After the six companions are copied and verified, press Enter (or select **Safely eject**) to request safe ejection of DAP Neo and both drives. Closing the window leaves it mounted. Windows uses the standard safe-removal API; macOS ejects every `bsd_name` under the NeoPod USB parent with `diskutil`. Close open player files/Explorer or Finder windows if the system refuses.

Without an image argument the helper selects the highest-numbered DP3 image in the repository release folder or alongside the helper. Its matching manifest must validate the image and all six companions before any firmware writes begin.

## Reset handling

The reset command packet follows Rockchip's implementation. The flow tries normal reset (subcode 0) first, then disconnect reset (4), and MSC boot (1) as the last fallback. The owner confirmed normal reset works while MSC boot does not on this Neo. Each mode uses live recovery-pipe checks for up to 12 seconds per attempt. Three consecutive absent probes are required before reporting that recovery disconnected. A reset response alone is never claimed as proof that firmware booted; the subsequent SD mount confirms that normal storage became available. Power-off and Maskrom reset modes are not used.

Reference: https://github.com/rockchip-linux/rkdeveloptool/blob/master/RKComm.cpp and RKComm.h.

## Companion update

The package layout is firmware image + `SHA256SUMS.txt` + `sd-card/.echo/`. The allowlist is `neo_scan.bin`, `neo_flac.bin`, `neo_mp3.bin`, `neo_aac.bin`, `neo_opus.bin`, and `boot.rgb565`. Both bare and package-relative checksum entries are accepted.

Automatic detection on Windows requires the NeoPod USB ancestry (VID 1209, PID 0001), ECHOPOD SD-card product name, and LUN 1. On macOS the same checks come from `system_profiler SPUSBDataType -json -detailLevel full`: a device with VID 1209/PID 0001, a LUN-1 media named like `ECHOPOD…SD`, and a mounted volume. Multiple matching volumes require an explicit choice. No generic removable drive or existing `.echo` folder authorizes automatic copying. Use `--sd-drive F:\` on Windows or `--sd-drive /Volumes/NeoPod` on macOS to select your MicroSD root explicitly; system volumes, subdirectories, and arbitrary folders are rejected.

Files are staged, flushed, backed up and read-verified. Only the six companions are replaced. Existing companions remain backed up inside `.echo/.neopod-update-*/backup/`; settings, saved devices, database, artwork and unrelated files are untouched. A detected replacement failure triggers rollback, with backups retained if the card becomes unavailable. A mount timeout reports the incomplete companion update and does not write to another drive.

## Options

- `--no-eject-prompt`: finish without waiting for Enter; the player remains mounted.
- `--sync-only`: validate the specified package and finish the SD copy without opening recovery, flashing or resetting.
- `--skip-companions`: firmware-only update, including stock image restoration.
- `--mount-timeout 300`: wait up to five minutes for the SD card.
- `--companions-dir PATH`: use another companion directory, still validated against the image's manifest.
- `--reset-only`: try the revised reset sequence without writing firmware or copying companions.

If no SD appears, check Storage mode, use pinhole reset if needed, and retry the same image with `--sync-only --sd-drive F:\` (Windows) or `--sync-only --sd-drive /Volumes/NeoPod` (macOS). No repeat firmware flash is needed after a successful flash.

## macOS notes

- The recovery device appears as Rockchip VID `0x2207`, PID `0x262d` with a vendor interface (class `0xff`, subclass `6`, protocol `5`). No kext or driver installation is required; the helper claims the interface through libusb-1.0 and uses the same CBW/CSW commands as `rkdeveloptool`.
- `brew install libusb` is required for the Python CLI. The packaged `.app` embeds `libusb-1.0.0.dylib`; it also checks `NEOPOD_LIBUSB`, Homebrew locations, `/usr/local/lib`, and `/opt/local/lib`.
- If USB enumeration fails, unplug and reconnect the player, confirm it is in Rockusb recovery (black screen), and check `system_profiler SPUSBDataType` for the `0x2207`/`0x262d` device.
- If macOS refuses ejection, close Finder windows previewing the player, then eject both NeoPod volumes from Disk Utility.

## Verification

Mock-only tests cover reset packets/modes/order, early USB detach, transient pipe disappearance, manifest mismatches before any USB access, delayed mount, ambiguous volumes, replacement rollback, preserved settings/library/devices, firmware-write failure, and the complete flash-to-copy order. They also parse recorded `system_profiler` JSON, reject wrong VID/LUN/media-name combinations, and check that every NeoPod media is ejected inner-first with `diskutil`. `tools/test_flash_neo_macos.py` runs on Windows too; libusb descriptor and real mount-point checks run on macOS. Tests use temporary directories and mocked USB. Physical reboot behavior still needs confirmation on a Neo.

## Recovery connection indicator

Enter recovery manually using Vol+ + Vol− + OK while connecting USB with the player powered off. Step 2 shows a green dot and **Connected in recovery mode** only when the Neo recovery pipe can be opened. A missing, stale, ambiguous or inaccessible interface shows a red dot and **Not connected in recovery mode**. Normal USB storage does not count. Unplugging the player disables **Start update** on the next check; selecting Start update also refreshes the connection first.

All automatic recovery-entry code and the experimental CLI option have been removed. Post-flash reboot to normal mode still uses normal reset (0) first.
