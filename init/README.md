# Amp-enable INIT (`MacMiniAudioFix`)

This is the foundation: a boot-time System Extension that enables the output-amplifier
GPIOs the screamer driver leaves disabled, restoring full-volume internal-speaker and
headphone audio. The Control Strip module rides on top of it.

It derives from **matouspikous's** original "MacMiniAudioFix - max sound" INIT
(`MacLegacyLabs`); the GPIO and Name Registry code is reused verbatim. This version factors
the amp control into named-state helpers (ON / MUTE / STANDBY) so the mute and auto-switch
work can call them. **Boot behavior is unchanged from v8** — amps are driven ON at startup.

## No prebuilt binary here — build it with CodeWarrior

Retro68 **cannot** emit a resident PowerPC `INIT` code resource (it only produces PEF apps
and shared libraries, not the 68K→PPC boot glue a classic INIT needs). So this INIT must be
built with **CodeWarrior on a PowerPC Mac**. If you already run matouspikous's INIT, you can
keep using it — this source is for people who want the factored/mute-ready version.

### CodeWarrior recipe (verified)

- **Project type:** PPC **Code Resource**
- **ResType / ResID:** `INIT` / `0`
- **File Type / Creator:** `INIT` / `MmAf`
- **Linker:** MacOS PPC Linker; **Entry Point:** `main`
- **Resource flags:** System Heap + Locked
- **Link libraries:** `InterfaceLib`, `NameRegistryLib`, `DriverServicesLib`
- Add `MacMiniAudioFix.r` to the project (or post-build `Rez MacMiniAudioFix.r -o <output> -append`)
  so the icons/`vers`/`STR ` are merged in.

Drop the built extension into `System Folder:Extensions:` and reboot. Full-volume audio at
boot = success.
