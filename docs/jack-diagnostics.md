# Optional NeoPod v1.8.0 jack diagnostics (developer build)

Production v1.8.0 has the owner-confirmed pull bit 1 fix and no diagnostic menu.
This optional developer build enables the **Diagnostics** entry and test controls.
It retains the 150 ms removal / 1.2 s insertion filter and Bluetooth handling.

Flash this variant's `DP3_V180.IMG` using the included graphical `flash-neo.exe`, or
`python flash-neo.py DP3_V180.IMG`. Both helpers copy and verify the six matching
SD companions. No library rescan is needed. Recovery entry uses the hardware
buttons; the app shows a live red/green connection indicator. Post-flash reboot
tries normal reset (0) first.

## This test: compare both raw pull-bit settings

Open **Main menu → Diagnostics** and find **Jack pull bit**. Current builds start
at **1**, the setting confirmed working by the owner on the historical v1.7.9 probe.
Press OK to compare **0**, or again to restore **1**. This only changes GPIO0.13's
pull register bit: the pin stays an
input, other pin configurations are untouched, and reboot restores the default.
The SDK's enable/disable enum and the helper's boolean name are inconsistent, so
the diagnostic deliberately labels the actual register bit instead of assuming
which electrical bias it provides.

For each setting (0 and 1), record **Jack raw** fully inserted and fully removed,
and whether it stays steady for ten seconds. Keep playback in the same state for
the comparison. Manual pause clears the automatic-resume latch if cycling gets
in the way. If a setting gives distinct stable levels, also report which level
corresponds to insertion; this build does not assume the polarity changed.

| Pull bit | Fully plugged raw | Fully unplugged raw | Stable or fluctuating? |
| --- | --- | --- | --- |
| 0 | | | |
| 1 | | | |

This is a controlled bias test, not a claimed auto-pause fix. Bluetooth handling
is unchanged. The DAC probe below is retained for comparison; no need to repeat
its previous test unless its readings change when selecting another pull bit.

Opening Diagnostics in this build enables the DAC's own presence-detect circuit
while it is already powered for wired playback. It preserves the inversion and
debounce fields, sets only the two documented enable bits in 0x0D0000, verifies
readback, then samples HP Status (0x0D0001) at most ten times per second. It does
not read clear-on-read interrupt registers, power up the DAC solely for a test,
or touch the DAC while Bluetooth is routed. Playback does not use this probe yet.

The DAC rows in Diagnostics are:

- **DAC HP link:** Ready, Start music, or Read failed.
- **DAC HP ctl:** detector control byte; normally 0xC4 after activation.
- **DAC HP bits:** raw status byte, not yet interpreted as headphone presence.

Jack raw, GPIO estimate, Raw changes and Jack changes retain the old GPIO-based
readings below. GPIO estimate is not an independently verified presence reading.

The owner observed GPIO0.13 **high** for fully inserted and fully removed
headphones, and **low** only during partial insertion with a normal three-contact
stereo plug. Thus this one signal cannot identify full insertion/removal; neither
inverting it nor lengthening debounce can distinguish equal readings.

On the previous build the owner observed control **0xC4** and status **0x20**
both fully plugged in and fully unplugged with music playing. Therefore that
probe has not provided a usable presence signal. This result does not prove the
DAC's HP_DETECT pin is connected to the Neo receptacle. Read failed remains an
error, not an unplug indication.

The recorded GPIO readings were identical fully in/out: 038277FC / 00000FA0 /
0000CD4B, with the backlight PWM changing GPIO2.4. Half insertion changed only
GPIO0.13 (038257FC). No clean GPIO insertion signal was identified by that test.

This probe preserves v1.7.5 pause/resume and Bluetooth behaviour so the readings
remain comparable. It is not a claimed fix. The GPIO counters exclude the initial
boot reading and do not write logs or settings to the SD card. The next step is to
identify a clean signal or validate the DAC's independent presence-detect path;
its existence does not prove that Fosi wired it on the Neo. The current DAC setup
writes 0x04 to 0x0D0000, leaving this detector disabled. The diagnostic probe sets
the documented enable bits, leaving other fields unchanged. A normal DAC restart
restores the usual setup before another probe. Reference: Cirrus CS43198 DS1156F2,
sections 4.4, 7.5.11 and 7.5.12:
https://statics.cirrus.com/pubs/proDatasheet/CS43198_DS1156F2.pdf

The repo comparisons confirm EchoPod Enhanced uses GPIO0.13 on the Echo Mini and
only pauses on unplug; it has no automatic wired resume to transplant. ReChord's
GPIO helpers agree with the current input/pull register layout. Neither establishes
the Neo's actual electrical levels, so this build captures those rather than
changing pin polarity or adding another speculative debounce delay.

Install `tools/requirements-jack-diagnostics.txt` into the build Python environment, then rebuild on Windows using `tools/build_jack_diagnostics.ps1`. The normal firmware
build leaves this diagnostic menu option off.
