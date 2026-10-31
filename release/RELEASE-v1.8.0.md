# NeoPod v1.8.0 — Production wired-headphone fix and Windows updater

Firmware: **DP3_V180.IMG** for the Fosi Audio DAP Neo.

GPIO0.13 now boots with **pull register bit 1**, the setting the owner confirmed
fixes stable wired unplug/replug detection on the v1.7.9 probe. The setting is
applied automatically on every boot. Removal pauses active playback and stable
reinsertion resumes an automatically paused session; manual pause stays paused.
The existing 150 ms removal / 1.2 s insertion filtering and working Bluetooth
handling are retained. GPIO2.8 remains DAC mute.

This is a production build: the home menu has no Diagnostics entry, the pull-bit
test control and transition counters are disabled, and the experimental DAC
headphone-detect probe does not run. Existing Stats, Bluetooth status indication,
saved devices and 3.5 mm-only output choices are retained.

## Install

Extract the complete ZIP into one folder. Set the player to USB Storage mode,
power off, then hold **Vol+ + Vol− + OK** while reconnecting USB. Release after
3–5 seconds; the screen stays black.

Open **flash-neo.exe**, choose **DP3_V180.IMG**, and follow the three pages.
Step 2 shows a live green/red recovery indicator. It does not enter recovery for
you. Step 3 keeps progress and the console visible together. Select **Safely eject**
after the matching companion update completes.

The Python command-line tool remains supported:

```powershell
python flash-neo.py DP3_V180.IMG
```

Both tools validate the image and all six SD companions, flash both slots, try
normal reset (0) first, then copy/read-verify the matching SD files. Keep USB
connected. If reset stalls, press the pinhole reset once while the helper waits.
Settings, library, artwork and Bluetooth records are preserved; no rescan is needed.

## Validation

The owner confirmed pull bit 1 fixes the wired behaviour on the diagnostic build.
The production package is built from that setting with diagnostic mode explicitly
off. ARM image bounds/header/CRC checks, focused host regressions, Python/GUI
tests, frozen GUI startup and package/ZIP checksum verification are performed by
the release workflow. The production image itself is ready for owner installation;
it has not been flashed automatically by the build process.
