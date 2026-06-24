# Mac Mini G4 audio under OS 9: real mute + automatic headphone switching (Control Strip module)

**TL;DR:** Building on matouspikous's "MacMiniAudioFix" amp-enable INIT, I added the two
things that were still missing for everyday use on the unsupported-G4 Mac Mini: a **real
mute** and **automatic speaker↔headphone switching**, packaged as a native **Control Strip
module**. Both are confirmed working on real hardware (Mac Mini G4 A1103, OS 9.2.2 via the
MacOS9Lives hack). Graduated (slider) volume is *not* solved yet, but I mapped out exactly
why and how far it can go — details at the end for anyone who wants to take a run at it.

---

## Background (the problem, for newcomers)

The MacOS9Lives install spoofs the Mac Mini G4 as a PowerMac5,1 (Cube) and injects a
"screamer"-compatible sound property, which gets I²S PCM flowing through the Intrepid
mac-io controller's built-in "Toonie" DAC. **There is no TAS3004 or any I²C audio codec on
this machine** — volume/amp control is done with GPIO pins, not codec registers. The
screamer driver sends its volume/mute/amp commands to a TAS3004 that physically isn't
there, and leaves the output amplifier GPIOs disabled. Result on a stock hack: near-silent
headphone jack, no internal speaker.

matouspikous's fix (the "MacMiniAudioFix" INIT) re-enables the amps at boot by writing to
GPIO pins in the Intrepid mac-io register block. That restores full-volume audio. This post
extends that work.

## GPIO map (Intrepid mac-io base + offset; base from the Name Registry `assigned-addresses`)

Single-byte GPIO pins. Bit 0 = output enable, bit 1 = active/status, bit 2 = data value.

| Offset | Role (confirmed by isolation testing) |
|--------|----------------------------------------|
| `0x67` | **Headphone detect** — bit 1: `1` = no HP, `0` = HP inserted |
| `0x6E` | amp enable — inert on its own |
| `0x6F` | **amp enable → HEADPHONE JACK** |
| `0x70` | **amp enable → INTERNAL SPEAKER** |
| `0x79` | amp enable — inert on its own (possibly master) |

Amp pin states that matter:

| Write | Meaning |
|-------|---------|
| `0x05` | output enable + data HIGH = **amp ON** (reads back `0x07`) |
| `0x01` | output enable + data LOW = **MUTE** (truly silent, incl. headphone jack) |
| `0x04` | output disabled + data HIGH = standby (silent on speaker, but historically leaked faintly on the HP jack) |

The pin→output mapping (`0x6F` = headphones, `0x70` = speaker) was found empirically by
enabling one amp pin at a time and listening. `0x6E`/`0x79` produced no sound alone.

## What's new

1. **Real mute.** Park the amp pins at `0x01` (output enabled, data driven LOW) instead of
   `0x05`. Clean, complete silence on both outputs — and because it drives the line low
   rather than just disabling the output stage (`0x04`), it doesn't leak on the headphone
   jack.

2. **Automatic speaker/headphone switching.** Poll `0x67` bit 1; when headphones are
   inserted, enable `0x6F` and park `0x70` (speaker) at standby, and vice-versa. On this
   hardware the switch is instant on insertion and ~1 s on removal (that lag is the
   detect-pin settling, not software).

3. **Delivered as a Control Strip module** — the native OS 9 idiom for a persistent widget.
   It shows `Spk`/`Hph`, switches automatically, and a click mutes/unmutes. Stays resident
   while the Control Strip is on, and works even when the strip is collapsed.

Install: keep the amp-enable INIT in Extensions (it provides boot audio), and drop the
Control Strip module into `System Folder:Control Strip Modules:`.

## Toolchain notes (for builders)

- The Control Strip module and the diagnostic app are built with **Retro68** (free, GCC-based
  PowerPC classic-Mac cross-compiler). A CSM is an `'sdev'` code resource wrapped in a Mixed
  Mode routine descriptor; ProcInfo `0x00003FF0` for
  `pascal long (long message, long params, Rect*, GrafPtr)`. Keep per-instance state in the
  refCon (heap), **not** fragment globals — mutable globals crash a Control-Strip-loaded
  code resource.
- **Boot INITs must be built with CodeWarrior**, not Retro68 — this Retro68 only emits PEF
  (apps/shared libs), not a resident PowerPC `INIT` code resource with the 68K→PPC boot glue.

## Graduated (slider) volume — status and an open challenge

Not solved, but here's the map so nobody re-treads dead ends:

- **There is no usable hardware/Sound-Manager volume on this machine.** The Toonie DAC has
  no gain registers. `GetDefaultOutputVolume` errors (`$8001`); `SetDefaultOutputVolume` and
  per-channel `volumeCmd` do nothing; the sole output component `'awgc'` (Grand Central
  Awacs) returns `$8002` for every `siHardwareVolume*` selector. So volume *must* be done by
  scaling PCM samples in software.
- **Scaling PCM works** — reducing 16-bit sample amplitude is audibly quieter on this DAC
  (no normalization). So graduated volume is physically possible.
- **Boot-time component capture works** — a CodeWarrior INIT can `RegisterComponent` +
  `CaptureComponent('awgc')` at startup and interpose on the output path (the captured
  `'awgc'` disappears from `FindNextComponent` and all audio routes through the wrapper).
  Runtime capture from an app is too late — the Sound Manager opens its output device once,
  early, and keeps it.
- **The wall:** a captured output-device wrapper that delegates to a *separately re-opened*
  `'awgc'` instance produces **silence** — no audio reaches the DAC even when we overwrite
  the buffer at both `SoundComponentGetSourceData` (pull) and `SoundComponentPlaySourceBuffer`
  (push). The re-opened instance apparently doesn't drive the I²S/DBDMA hardware the way the
  Sound Manager's own setup does.

**If you know how the Sound Manager wires its output device to the Intrepid I²S/DBDMA on
these machines — or have made an interposing/effect sound output component work on OS 9 —
I'd love pointers.** The alternative route (scaling the DBDMA transmit buffer at
`mac-io + 0x8200` directly) is untried; it's complicated by physical-vs-logical addressing
and a live-DMA race.

## Credit

Amp-enable foundation and the original reverse-engineering: **matouspikous**
(`MacLegacyLabs`). Mute, pin mapping, auto-switch, the Control Strip module, and the Tier-3
investigation built on top of that. Source available on request.
