# Prebuilt binaries

Install these directly — no compiler needed. (Both are also reproducible from
source with `../build.sh`; see the top-level README.)

| File | What it is | Where it goes |
|------|------------|---------------|
| `MiniAudioStrip.img` | The **Mini Audio Control Strip module** — auto speaker/headphone switching + click-to-mute. **This is the thing you want.** | Mount the image, copy `MiniAudioStrip` into `System Folder:Control Strip Modules:`, reboot (or toggle the Control Strip). |
| `MiniAudioTool.img` | Optional **diagnostic app** — read GPIO pins, drive the amps by hand, test routing/tone. Useful for validating on your own machine. | Mount and run it from anywhere; it touches nothing at boot. |
| `*.bin` | MacBinary copies of the above, in case you can only transfer a single fork-encoded file (decode with StuffIt Expander). | — |

**Prerequisite:** you must already have an amp-enable INIT installed (matouspikous's
"MacMiniAudioFix", or the one built from `../init/`). It provides boot audio; the Control
Strip module rides on top of it. Without it, the amps are disabled and you'll hear nothing.

`.img` files are Apple-Partition-Map disk images — mount them on the OS 9 machine (Disk Copy
or just double-click) and drag the contents out.
