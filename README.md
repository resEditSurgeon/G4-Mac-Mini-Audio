# G4 Mac Mini Audio — Mac OS 9 mute + headphone auto-switch

Real **mute** and automatic **speaker↔headphone switching** for the unsupported-G4 Mac Mini
(A1103, spoofed as PowerMac5,1) running **Mac OS 9.2.2** via the MacOS9Lives hack — packaged
as a native **Control Strip module**. Both are confirmed working on real hardware.

Built on top of **[matouspikous's](https://github.com/matouspikous) "MacMiniAudioFix"**
amp-enable INIT (`MacLegacyLabs`), which restores full-volume audio at boot. This project
adds the everyday-use pieces that were still missing. See **[CREDITS](#credits)**.

> ⚠️ **Graduated (slider) volume is not achievable in software on this machine.** There's no
> hardware gain, and the Sound Manager opens its output device *and* the mixer before any INIT
> can interpose — so a component wrapper is never called (confirmed on hardware). The only
> untried avenue is driving the DBDMA buffer directly. Full investigation + open challenge in
> [COMMUNITY-POST.md](COMMUNITY-POST.md); pointers welcome.

## The hardware problem (short version)

There is **no I²C audio codec** on this machine (no TAS3004). Volume/amp control is done
through GPIO pins in the Intrepid mac-io register block, not codec registers. The screamer
driver talks to a codec that isn't there and leaves the output amps disabled — so a stock
hack gives near-silent audio. matouspikous's INIT re-enables the amps; this project adds
mute and routing on top.

| GPIO offset | Role (confirmed by isolation testing) |
|-------------|----------------------------------------|
| `0x67` | Headphone detect — bit 1: `1` = no HP, `0` = HP inserted |
| `0x6F` | amp enable → **headphone jack** |
| `0x70` | amp enable → **internal speaker** |
| `0x6E`, `0x79` | amp enable — inert on their own |

Amp pin states: `0x05` = ON, `0x01` = **MUTE** (truly silent, incl. the HP jack), `0x04` =
standby. Full detail in [COMMUNITY-POST.md](COMMUNITY-POST.md).

## What's in here

```
init/                 Amp-enable INIT (CodeWarrior source) — the foundation
csm/mini-audio-strip/ The Mini Audio Control Strip module (Retro68 source)
tools/MiniAudioTool/  Interactive GPIO/amp/routing diagnostic app (Retro68 source)
prebuilt/             Ready-to-install .img disk images (+ MacBinary fallbacks)
scripts/wrap-apm.py   Wraps a raw HFS image in an Apple Partition Map so OS 9 mounts it
build.sh              Builds the Control Strip module + the diagnostic app
```

## Install (no compiler needed)

1. **Amp-enable INIT** — keep matouspikous's "MacMiniAudioFix" in `System Folder:Extensions:`
   (or build the version in [`init/`](init/README.md) with CodeWarrior). This provides boot
   audio; everything else depends on it.
2. **Control Strip module** — mount `prebuilt/MiniAudioStrip.img`, copy `MiniAudioStrip` into
   `System Folder:Control Strip Modules:`, reboot. The tile shows a **speaker / headphones /
   mute icon** for the current state, auto-switches on headphone insert, and **click to
   mute/unmute**.
3. **(Optional) diagnostic app** — `prebuilt/MiniAudioTool.img` lets you read GPIO pins and
   drive the amps by hand. It never touches the boot path.

See [`prebuilt/README.md`](prebuilt/README.md) for details.

## Build from source

The Control Strip module and the diagnostic app build with
**[Retro68](https://github.com/autc04/Retro68)** (PowerPC):

```sh
RETRO68=$HOME/Retro68-build ./build.sh   # outputs into prebuilt/
```

The **INIT** must be built with **CodeWarrior** on a PowerPC Mac — Retro68 cannot emit a
resident PowerPC `INIT` code resource. Recipe in [`init/README.md`](init/README.md).

## Status

- ✅ **Real mute** — park the amp pins at `0x01` (driven low, no HP-jack leak).
- ✅ **Headphone auto-switch** — poll `0x67`, route `0x6F`/`0x70`. Instant on insert,
  ~1 s on removal (detect-pin settling, not software).
- ⛔ **Graduated volume** — not achievable in software here (component layer unreachable from an
  INIT; only the DBDMA route remains, untried). See the write-up in [COMMUNITY-POST.md](COMMUNITY-POST.md).

## Credits

- **Amp-enable foundation & original reverse-engineering:** matouspikous (`MacLegacyLabs`,
  "MacMiniAudioFix - max sound"). The GPIO and Name Registry code here is reused from that
  release.
- **Mute, pin mapping, auto-switch, Control Strip module:** this project.

> **Licensing:** this builds on matouspikous's work — confirm their terms before reuse. A
> `LICENSE` file has intentionally not been added yet; see the maintainer.
