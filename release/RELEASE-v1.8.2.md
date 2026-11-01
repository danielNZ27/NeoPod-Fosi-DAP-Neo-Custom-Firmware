# NeoPod v1.8.2 — Menu scrolling stops at the ends

Firmware: **DP3_V182.IMG** for the Fosi Audio DAP Neo.

Menu and list selection no longer wraps. Pressing up on the first item or down on
the last item keeps the selection in place instead of jumping to the opposite end
of the list. Fast scrolling and section jumps already stopped at the ends, so all
navigation now behaves consistently.

This release keeps the v1.8.0 production fixes: GPIO0.13 boots with **pull
register bit 1**, the setting the owner confirmed fixes stable wired unplug/replug
detection, applied automatically on every boot. Removal pauses active playback and
stable reinsertion resumes an automatically paused session; manual pause stays
paused. The existing 150 ms removal / 1.2 s insertion filtering and working
Bluetooth handling are retained. GPIO2.8 remains DAC mute.

This is a production build: the home menu has no Diagnostics entry, the pull-bit
test control and transition counters are disabled, and the experimental DAC
headphone-detect probe does not run. Existing Stats, Bluetooth status indication,
saved devices and 3.5 mm-only output choices are retained.

## Install

Extract the complete ZIP into one folder. Set the player to USB Storage mode,
power off, then hold **Vol+ + Vol- + OK** while reconnecting USB. Release after
3–5 seconds; the screen stays black.

Open **flash-neo.exe**, choose **DP3_V182.IMG**, and follow the three pages.
Step 2 shows a live green/red recovery indicator. It does not enter recovery for
you. Step 3 keeps progress and the console visible together. Select **Safely eject**
after the matching companion update completes.

The Python command-line tool remains supported:

```powershell
python flash-neo.py DP3_V182.IMG
```

Both tools validate the image and all six SD companions, flash both slots, try
normal reset (0) first, then copy/read-verify the matching SD files. Keep USB
connected. If reset stalls, press the pinhole reset once while the helper waits.
Settings, library, artwork and Bluetooth records are preserved; no rescan is needed.

## Validation

The selection clamp is covered by host regressions for pressing at both ends of a
list. ARM image bounds/header/CRC checks, focused host regressions, Python/GUI
tests, frozen GUI startup and package/ZIP checksum verification are performed by
the release workflow. The production image itself is ready for owner installation;
it has not been flashed automatically by the build process.
