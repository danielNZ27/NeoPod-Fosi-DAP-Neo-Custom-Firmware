# Bluetooth on NeoPod

Bluetooth audio is available from v1.7.0 on the Fosi Audio DAP Neo. The owner
confirmed playback and the play/pause/skip pop fix with the V170 image on a
physical player. Compatibility with every headphone or sample rate is not
claimed.

## Turn Bluetooth on or off

Open **Settings → Audio → Bluetooth**, select **Bluetooth** and press OK.
Bluetooth defaults to Off on a fresh setup. Your chosen On/Off setting is saved.
Turning it off preserves your saved devices. Back leaves the menu without
turning Bluetooth off.

## Pair new headphones or a speaker

1. Choose **Pair new device**. This enables Bluetooth if necessary and starts
   discovery once the module is ready.
2. Put the headphones or speaker into pairing mode.
3. Select its name with the wheel and press **OK**.
4. Wait for **Connected**. Audio starts automatically once the connection settles.

Finding a device does not pair it. Only selecting its result requests a new
pairing. Background reconnect is paused while browsing Pair new device;
leaving cancels discovery. Choose Search again if needed.

## Saved devices and automatic reconnect

Up to six device addresses/names are stored in **`.echo/bluetooth.bin`** on the
MicroSD card. While Bluetooth is On, NeoPod tries the preferred saved device
first, then other saved devices. An unavailable device is retried later; allow
several seconds after switching headphones on. Automatic reconnect uses the
reconnect command and never falls back to a new pairing request.

Under **Saved devices**, choose a device and select:

- **Connect:** connect to that saved device, enabling Bluetooth if necessary.
- **Disconnect:** end the connection and suppress automatic reconnect until
  you choose Connect or toggle Bluetooth Off/On.
- **Forget device:** confirm removal of the saved NeoPod record and disconnect
  it if active. It will no longer be targeted for automatic reconnect.

Forget does not erase the headphones' own saved pairing record or claim to
erase the module's internal bond-key database. Remove the player from the
headphones' remembered devices as well if you want to clear their record.

## Playback transitions

The module stays connected while paused. NeoPod fades PCM to silence and keeps
feeding zero samples instead of leaving the I2S input without data. Play
resumes from the paused stereo frame with a fade-in. Same-rate track loading
uses the same silent feed. A sample-rate change still requires hardware clock
reconfiguration with module muting around it.

The tested pop fix also holds the external DAC mute/volume controls steady
during Bluetooth pause/resume and settles DAC initialization before module
unmute. Normal wired playback and explicit volume controls remain available.

## Troubleshooting

- **No devices appear:** check headphone pairing mode and select Search again.
- **Cannot add a device:** the list holds six devices; forget one first if full.
- **Device does not reconnect:** check Bluetooth is On. If you chose Disconnect,
  choose Connect or toggle Off/On to resume automatic reconnect.
- **Could not save settings:** check the MicroSD card is present and writable.
- **Unexpected disconnect:** turn Bluetooth Off before connecting USB. If
  available, `.echo/neo-bt.txt` contains the recent UART commands, replies and
  state changes for a bug report.

When updating, preserve `bluetooth.bin`, settings, library and artwork. Replace
the matching firmware/scanner files listed in the release instructions.

## Connection indication (v1.7.2)

Reconnect-command acknowledgements alone do not indicate a connection. NeoPod queries the module and waits for a complete connected status report before showing Connected or starting Bluetooth audio. While headphones are off it can keep retrying, but the top-bar icon should continue flashing. It becomes solid only after the module reports a connected link.

## Automatic pause and resume (v1.7.3)

Losing the Bluetooth connection pauses music that was playing. Once headphones reconnect and their audio stream is ready, NeoPod resumes that automatically paused session. Music you paused yourself stays paused. Manual play/pause cancels pending automatic resume. Wired headphone unplug/replug uses the same behavior; wired jack changes are ignored during a connected Bluetooth session.
