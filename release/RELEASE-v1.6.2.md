# NeoPod v1.6.2 — iPod Classic theme, boot artwork and browser fixes

Firmware image: `DP3_V162.IMG`. Changes since v1.6.1 for the Fosi Audio DAP Neo.

## New iPod Classic theme

- Choose **Settings → Display & Controls → Theme → iPod Classic**.
- Silver gradient headers, light menus, bold dark text and blue selection bars.
- Full-height home menu in the classic style.
- Dark Now Playing screen with larger, 184 × 184 artwork, song title, artist and blue progress bar.
- Keeps the Neo's wheel navigation, previous/next, play/pause, volume, shuffle/repeat and seek controls, including seek instructions.
- Simplifies the classic status bar to icons without the numeric battery percentage.
- The original **NeoPod** theme remains available, with its existing layout and saved button colour. Theme selection persists after restarting; older settings default to NeoPod.

## Boot artwork

- Replaces the plain NeoPod startup text with the supplied full-screen player artwork, resized to **240 × 320** without cropping.
- Fades in from black and works with either theme.
- Streams `/.echo/boot.rgb565` from the SD card into the existing drawing buffer instead of allocating a full-screen image buffer.
- Falls back to the text splash if the image is missing, unreadable or the wrong size.
- Adds a reproducible artwork conversion step to the firmware packaging script.

## Cover Flow animation

- Uses faster nearest-neighbour artwork sampling while covers move, restoring bilinear smoothing when they settle.
- Limits animation time steps so slow display transfers or storage reads do not skip directly through a transition.
- Keeps Cover Flow text readable against its dark background in the iPod theme.

## Browser fixes and metadata

- Fixes Folders, All Songs, Albums and Artists showing eight labels while navigation allowed twelve rows. The browser now caches all **12 visible rows**, eliminating the four blank selectable rows.
- All Songs displays **Song name - Artist**, using each track's artist metadata.
- Long selected entries scroll horizontally after a short pause; other rows retain their normal truncated display. Album-track and folder naming remain unchanged.

## Header alignment and version reporting

- Moves header titles down two pixels for better alignment on the physical screen.
- Moves playback, battery, charging and battery-percentage indicators down one pixel from their original positions, in both themes.
- Corrects the device version string to **NeoPod v1.6.2** and updates the firmware image version/build date.
- Extends the code allocation within the stock Module 0 capacity to accommodate the new UI code, without overlapping high RAM.

## Upgrade from v1.6.1

1. Download **NeoPod-v1.6.2.zip** and extract it.
2. Merge **sd-card/.echo** into the `.echo` folder at the root of your MicroSD card. Include **boot.rgb565** and the matching **neo_scan.bin**; preserve your existing settings, library database and covers.
3. Flash **DP3_V162.IMG** using the existing Rockusb recovery procedure, or copy it to the root of internal storage when using the stock bootloader update method. See the included README for instructions.
4. Restart the player. Select iPod Classic in Settings if desired.

For Rockusb flashing from the extracted ZIP directory on Windows, run `python flash-neo.py DP3_V162.IMG` after putting the player in recovery mode.

No music rescan or cover rebuild is required when upgrading from v1.6.1. The boot artwork requires the new SD file; flashing only the firmware keeps the text fallback.

The ZIP includes the firmware, all matching SD companions, artwork, flashing script, documentation and licence files. Individual assets and SHA-256 checksums are also attached.

## Validation

- All **15 targeted test suites passed**, covering artwork loading/fading/fallback, theme selection and persistence, preserved seek controls, twelve-row browsing in both themes, Song–Artist labels, Cover Flow, cover loading, image drawing and status/header layout.
- Refreshed the cover test fixture to the 128 × 128 / 64 × 64 dimensions used since v1.6.1.
- Release firmware builds within the linker limits; the image packager verifies header copies and CRC-32.
- The owner confirmed the UI changes on the physical Neo during development. The final version-number rebuild has not been flashed separately.
- The broader legacy `test_app` suite is not clean for the Neo configuration: it still contains EchoPod menu/control assumptions and fails/crashes. This release does not claim a passing full test suite.

**Full comparison:** https://github.com/danielNZ27/NeoPod-Fosi-DAP-Neo-Custom-Firmware/compare/V1.6.1...V1.6.2
