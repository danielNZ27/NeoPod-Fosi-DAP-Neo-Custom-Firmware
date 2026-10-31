# Neo Bluetooth bring-up

## Published checkpoint: v1.7.0 / V170

The owner confirmed V170 works and resolves the reported popping on the physical
Neo/headphone setup, then requested publication. Release v1.7.0 uses the exact
tested image, SHA256
`f6942e7053e1d25f232c6c36708472f699fffb334f24ccbd4d0479e2e2a1a558`.
The source and assets are published together. The numbered development notes
below record intermediate hypotheses and their validation status at the time;
the current hardware confirmation supersedes V170's earlier pending status.
See [user guide](bluetooth.md) and [release notes](../release/RELEASE-v1.7.0.md).

## V170 DAC mute isolation

V169 feedback: continuous playback works and sounds better, but play/pause/next
still pop. Preserve the V169 PCM/silence transport improvement. The remaining
pop source is not hardware-confirmed.

The continuous-silence path still called dac_path_set_muted(true/false), which
writes the external DAC volume and toggles Neo GPIO2.8 during every transition.
V170 leaves that hardware state steady while Bluetooth is routed, DAC setup is
complete and the DAC is already unmuted. PCM/silence DMA provides pause muting.
New DAC initialization can still unmute once. Full teardown/rate setup retains
its required hardware sequence. Explicit volume changes remain supported.

For initial start and rate changes, DAC unmute/settling now happens before the
Bluetooth module is enabled. The existing zero lead/fade-in prepares PCM before
module unmute. When Bluetooth is switched off during a silent pause, retain the
PCM resume fade as the transport rejoins wired playback.

ARM build, image checks and the five existing focused Bluetooth/PCM/DMA tests
pass. Those host tests do not verify electrical mute-pin transients; V170 is
a focused hardware listening test of the external-DAC mute hypothesis.

## V169 silent PCM transitions

User confirmed V168 works; Bluetooth still pops on play, pause and next track.
V168 is the current physically working connection/playback baseline.

The previous ramp changed only ten milliseconds of upcoming PCM, then left
full-level samples immediately after it. The 17 ms wait crossed the ramp's
end before module muting, allowing a return to full level. Pausing also
suspended the music DMA while I2S remained enabled without a silence source.

V169 keeps the V168 pairing/reconnect policy and changes the audio transport:
- Fade out ten milliseconds and clear a following silent tail.
- After the fade, freeze/capture the music DMA position and switch to a separate
  cyclic all-zero DMA descriptor. I2S and the BT stream remain running through
  pause and same-rate manual track changes. Decoder/file work happens while
  hardware continues feeding silence; music time stays frozen during pause.
- Resume with a partial first descriptor at the paused stereo frame, then
  rejoin the normal ring at the next block. Fade real PCM in after any remaining
  silent tail so ramping zeros does not produce a late full-level onset.
- Before module unmute/start, prepare a short zero lead and PCM fade-in while
  the module is still muted, then allow DMA to reach the zero region.
- Paused seek retains the silence DMA, and full shutdown/rate changes mute the
  module before I2S teardown. Rate changes still require clock reconfiguration;
  continuous clocking applies to pause and same-rate track changes.

Tests cover zero-tail continuity across ring wrap, fade-in after the zero tail,
stereo-aligned DMA resume at mid-block/end-of-ring and invalid offsets, plus
the existing Bluetooth pairing/reconnect/PCM tests. All five focused test
executables and ARM builds pass. Hardware listening validation remains pending.

## V168 explicit pairing correction

User clarification: being discoverable/present in Pair new device is not
authorization to pair. Only clicking a result authorizes a new pairing.

V167 incorrectly reused CL06 (pair/connect) for saved-device background
reconnects. V168 uses CH06 for saved-device reconnect and its retry; it never
falls back to CL06. Only an explicit Pair new device selection issues CL06.
The manager tracks pending new-pair intent separately from saved reconnects,
and only the former can create a saved entry. Name-based matching is removed.

Entering Pair new device cancels an in-progress connection attempt, pauses
background reconnects, and rejects unsolicited connections while browsing.
It leaves saved entries intact. Leaving discovery permits the normal saved
reconnect policy again unless the user explicitly selected Disconnect.

Regression tests verify discovery with repeated service updates produces zero
connect/pair calls and no saved entry until clicking a result; saved reconnects
use CH06 on initial/retry frames; the Pair menu cancels/pauses reconnect;
unsolicited same-name/different-address connections are rejected. All four
focused Bluetooth/audio executables and the ARM build pass. PCM fade/mute
changes and the diagnostic trace from V167 are retained. The earlier hardware
audio cut-out is not claimed resolved by these software tests.

## V167 recovery after V165/V166 hardware regression

The user reports V166 still plays about one second, abruptly cuts out, and the
headphones appear to switch off without a power-off prompt. V164 remains the
last physically confirmed continuous-playback baseline; exact cut-out cause
has not been captured and the V166 hypothesis was insufficient.

Changes in V167:
- A pending explicit connection request authorizes its selected target even if
  the first status reports another/incomplete address. V166 latched approval
  only after this check, leaving the first-report rejection path intact.
- Incoming saved-device status can match a unique exact nonempty saved name
  when its reported address differs. Duplicate names do not match this way.
- Wait one second of approved connection before automatic stream enable.
- Use the working CL06 address command for initial and retry attempts, including
  saved devices, rather than initiating saved-device connections with CH06.
- Remove optional C86/C87 play/pause notices. CY0/1E and CY0/00 still enable and
  mute module audio. Physical testing is needed to confirm the module needs no
  additional notice on this board.
- Preserve V165 PCM fades, stable GPIO2.1 routing across pause/resume and module
  muting before DMA/I2S interruptions. This is not a rollback of click suppression.
- Capture a 64-entry UART TX/RX/audio/state history in RAM. Save to reserved
  .echo/neo-bt.txt after a drop/disconnect/off while no longer connected; do not
  write the trace during active Bluetooth playback. T=TX, R=RX, S=state change,
  A=audio mute request, P=power. Explicit Off can flush a still-pending report.

Tests cover first-status address mismatch while a requested connection is
pending, the one-second settle interval, quiet CY-only stream commands, CL
retry frames, saved-device policy and PCM ramps. Four focused executables and
the ARM build pass. On-device recovery and click suppression remain unverified
until the user tests V167. Do not claim the shutdown/cut-out cause is confirmed.

## V166 connection stability correction

V165 hardware feedback: pairing succeeds and audio plays briefly, then Bluetooth
disconnects/reconnects. Exact on-device cause has not been captured.

The stock status parser at mod0 03065aac..03065abe accepts both `CQO` and `CQo`
as connected, and only `CQS`, `CQL`, `CQI` as explicit disconnected statuses.
Our inherited parser accepted only uppercase O and disconnected on any other
letter. V166 follows stock and ignores connected reports missing their address.

The new V165 manager also rechecked every connected report against saved MACs,
and disconnected an already-approved session if a later report differed.
V166 latches the selected/approved peer for the active connected session, so
subsequent status payloads do not provoke a disconnect or change the saved
identity. A real disconnect, explicit disconnect, radio toggle or Forget clears
that approval. Initial unsolicited unknown devices are still disconnected.

Regression tests cover lowercase replies, unknown/short status preservation,
explicit CQI disconnect, changed status identity during an approved session,
and Forget still rejecting subsequent unknown connections. ARM build and all
four focused Bluetooth/audio test executables pass. Physical stability remains
to be verified by the user. Audio gating/fades from V165 are retained.

## Confirmed V164 playback

The user confirmed V164 wireless playback works. GPIO2.1 switching is now
hardware validated for the connected headphones. Preserve V164 as the known
working audio baseline in `build/bluetooth-route-test/DP3_V164.IMG`.

## V165 Bluetooth menus and quieter transitions

- Settings > Audio > Bluetooth opens a normal list without enabling the radio.
  Fresh/invalid storage defaults to Off. Explicit On/Off is persisted.
- Pair new device enables Bluetooth if needed and starts discovery once ready.
  Select a result to pair; a confirmed connection is saved and audio is enabled
  automatically. Leaving discovery cancels the scan. Back does not turn off BT.
- Saved devices offers Connect/Disconnect and Forget device with confirmation.
  Connect can enable the radio and can switch from another connected device.
- Up to six addresses/names and the preferred device are stored in a versioned,
  checksummed `/.echo/bluetooth.bin`. Do not distribute or overwrite this file
  with firmware updates. The main loop saves outside display-strip service.
- While enabled, the preferred saved device is tried first with stock's CH06
  reconnect command; unavailable devices are retried in round-robin order, with
  bounded eight-second attempts and two-second spacing. Incoming connections
  from remembered devices are accepted. Manual Disconnect suppresses reconnect
  until an explicit Connect or radio toggle.
- Forget removes NeoPod's saved entry, disconnects it, persists the removal,
  and rejects unsolicited connections from the forgotten address. No verified
  per-device bond-key erase command for the module has been identified, so this
  does not claim to erase the module's own internal key database or the record
  in the headphones. A device can be explicitly paired again from discovery.
- For Bluetooth pause/manual track changes/rate changes, apply a 10 ms digital
  PCM fade where buffered data permits, send CY0/0 before stopping/suspending
  DMA, and allow 20 ms for module muting. Resume/new output ramps valid PCM and
  enables the module only once DMA/I2S is running. Hold GPIO2.1 routing stable
  across pauses; radio state/connection and temporary audio muting are separate.
  Same-format gapless playback retains its continuous DMA path.

Reconnect/menu/storage/PCM tests and ARM builds pass. The full host suite has
111 passes and nine failures; an independently archived, unchanged HEAD build
reproduces all nine failures (toast, app, test-pattern, DAC sequence, key ladder,
panel, audio engine, cover grid and no-comments). V165 reconnect and click
reduction still require listening/reconnect tests on the physical Neo.

UI previews are rendered from the production screen at 240 x 320 under
`build/bluetooth-ui-preview/`.

## Hardware feedback, 31 October 2026

V163 pairs with the user's headphones. With the stream command enabled the
screen reports Streaming, RX 960 rising above 1191, 56 frames, OE 0, FIFO 21/0.
Recent bytes: `80 AC 57 48 2D 31 30 30 30 58 4D 35`.
The name tail reads WH-1000XM5. Wireless audio remains silent.
This verifies discovery/pairing and bidirectional UART control on this unit;
Streaming is a local command state, not an acknowledgement of received PCM.
FIFO depth zero is not proof of an absent FIFO: observed RX peak is 21.

## Stock evidence (local DP3_V137.img)

SHA256 fcc1cd3e35b46f1f8dbcb8da39fcc5eb3f1d469cdfade5e9c6ea3bd71ac6deef.

- mod81 0307d164..0307d1a4 powers GPIO2.7 and opens UART at 115200;
  mod21 0307d1b6..0307d290 waits for CV1 and handles version 5.
- Resident 03065756..03065770 builds C Y 0 1E 00; mod81
  0307deca..0307dece enables that command when the link is active.
- mod33 0308a1e4..0308a25c tests powered (+0x2d4) and connected (+0x2d3).
  When the latch at 0302314c permits a transition, it drives GPIO2.1 HIGH
  for BT and LOW for wired. The latch is inverted after the write, preventing
  repeated GPIO writes until the connection state changes.
- Stock mod0 03050aa4..03050b0c uses 0x401f for the 24-bit path, the same
  I2S TXCR format as NeoPod. It is not evidence for changing word format.

## V164 routing experiment

Apply the observed GPIO2.1 high/low route only while the module stream is
enabled and the DAC path has completed initialization. Reassert during service
so a later track/rate startup is followed by the route. On stream disable,
disconnect or Bluetooth power-off restore GPIO2.1 low if DAC is running.
Do not write GPIO2.0 (DAC enable) or GPIO2.14 (OK input) as Echo Mini switches.
Do not touch pin state while the DAC path is down; its shutdown/init own it.

This is a controlled routing experiment, not confirmed wireless playback.
If silent, investigate the remaining PCM interface/clock routing against stock;
do not treat increasing UART RX or the Streaming label as PCM delivery proof.
