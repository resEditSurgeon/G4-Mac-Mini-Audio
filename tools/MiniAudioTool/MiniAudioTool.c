/*
 *  MiniAudioTool.c  —  v3  (Tier 1 mute + Tier 2 routing probe & auto-switch)
 *
 *  A double-clickable PowerPC application for Mac OS 9.2.2 on the
 *  "unsupported G4" Mac Mini (A1103, spoofed as PowerMac5,1 / Cube).
 *
 *  WHY AN APP, AND WHAT IT VALIDATES
 *  ---------------------------------
 *  Claude cannot test on hardware, so this interactive app is the instrument:
 *  click, listen, read the GPIO hex on screen. It never touches the boot path;
 *  revert = quit / drag to Trash. It reuses the v8 INIT's GPIO + Name Registry
 *  code verbatim, so anything proven here transfers to the production INIT.
 *
 *  CONFIRMED ON HARDWARE
 *  ---------------------
 *    Tier 1 (2026-06-22): amp pins drive AMP_ON/MUTE/STANDBY as written;
 *      MUTE ($01) silences cleanly. $01 chosen for production mute.
 *    Tier 2 routing (2026-06-22): 6F -> headphone jack, 70 -> internal
 *      speaker, 6E & 79 -> no audible output on their own.
 *
 *  THIS BUILD (Tier 2 auto-switch)
 *  -------------------------------
 *  An "Auto-switch" toggle polls the headphone-detect pin (0x67 bit 1) in the
 *  event loop and routes audio: headphones in -> 6F on / 70 muted; headphones
 *  out -> 70 on / 6F muted. This validates the switching *logic*. The resident
 *  interrupt-time version (Time Manager) lives in the CodeWarrior INIT, which
 *  has the full Time Manager API the Retro68 multiversal headers lack.
 *
 *  Build: Retro68, PowerPC app, links InterfaceLib / NameRegistryLib /
 *         DriverServicesLib (same as the INIT).
 */

#include <Types.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Sound.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <NameRegistry.h>
#include <DriverServices.h>
#include <Processes.h>
#include <Files.h>
#include <Resources.h>
#include <CodeFragments.h>

/* ---- GPIO pin assignments (identical to v8 INIT) ---- */
#define GPIO_HP_DETECT  0x67    /* bit 1: 1=no HP, 0=HP inserted */
#define GPIO_HP_ALT     0x51    /* changes with HP insertion (bit 7) */
#define GPIO_AMP_X      0x6D    /* amp-related, reads back 0x03 */
#define GPIO_AMP_1      0x6E    /* amp enable (inert on its own) */
#define GPIO_AMP_2      0x6F    /* HEADPHONE jack  (confirmed) */
#define GPIO_AMP_3      0x70    /* INTERNAL SPEAKER (confirmed) */
#define GPIO_AMP_4      0x79    /* amp enable (inert on its own) */

#define AMP_ON          0x05    /* output enable + data HIGH = ON   */
#define AMP_MUTE        0x01    /* output enable + data LOW  = MUTE */
#define AMP_STANDBY     0x04    /* output disabled + data HIGH = OFF*/

/* ---- Dialog item numbers (must match MiniAudioTool.r DITL 128) ---- */
#define kBtnAmpsOn      1
#define kBtnMute        2
#define kBtnStandby     3
#define kBtnRead        4
#define kBtnTone        5
#define kBtnQuit        6
#define kBtnAuto        7   /* Auto-switch on/off */
#define kBtnGrp123      8   /* ONLY 6E+6F+70 (no 79) */
#define kBtnOnly79      9   /* ONLY 79 */
#define kBtnOnly6E      10
#define kBtnOnly6F      11
#define kBtnOnly70      12
#define kTxtTitle       13
#define kTxtStatus      14
/* item 15 = static routing label (set in .r) */
#define kBtnVolRead     16  /* Tier 3 volume probe */
#define kBtnVol100      17
#define kBtnVol50       18
#define kBtnVol25       19
#define kBtnVol0        20
/* item 21 = static volume label (set in .r) */

#define kBtnProbe       22  /* Tier 3 sound-component probe */
#define kBtnDiag        23  /* Tier 3 read SoftVol INIT diag block via Gestalt */

/* Sound Manager default-output volume: a long, low word = left, high word =
 * right, each 0x0000..0x0100 (0x0100 = unity). Declared via the headers and
 * exported from InterfaceLib. */
#define VOL_BOTH(pct16)  (((long)(pct16) << 16) | (long)(pct16))

/* Component Manager (FindNextComponent/OpenComponent/GetComponentInfo), the
 * SoundComponent* calls, kSoundOutputDeviceType and the si* selectors are all
 * declared by <Components.h>/<Sound.h>. SoundSource is OpaqueSoundSource* (a
 * pointer); pass 0 (NULL) for global/hardware selectors. READ-ONLY probe. */
#include <Components.h>

#define kSleepTicks     6      /* WaitNextEvent sleep ~ 10 polls/sec */

static UInt32        gMacIOBase = 0;
static SndChannelPtr gToneChan  = nil;
static Boolean       gAutoOn    = false;
static UInt8         gLastDetect = 0xFF;   /* force first apply */

/* Tier 3 runtime-capture experiment state */
static Component           gWrapReg    = 0;   /* our registered wrapper       */
static Component           gAwgcComp   = 0;   /* the real 'awgc' we captured  */
static Component           gCaptureRef = 0;   /* ref to open the REAL 'awgc'  */
static ComponentRoutineUPP gWrapUPP    = 0;
static long                gWrapOpens  = 0;   /* times SM opened our wrapper  */
static long                gWrapPlays  = 0;   /* PlaySourceBuffer calls seen  */

/* ============================================================= */
/* GPIO access — verbatim from the v8 INIT                       */
/* ============================================================= */

static UInt8 ReadGPIO(UInt32 offset)
{
    volatile UInt8 *a;
    UInt8 v;
    a = (volatile UInt8 *)(gMacIOBase + offset);
    v = *a;
    SynchronizeIO();
    return v;
}

static void WriteGPIO(UInt32 offset, UInt8 value)
{
    volatile UInt8 *a;
    a = (volatile UInt8 *)(gMacIOBase + offset);
    *a = value;
    SynchronizeIO();
}

/* ============================================================= */
/* Name Registry mac-io discovery — verbatim from the v8 INIT    */
/* ============================================================= */

static OSStatus FindIOControllerBase(void)
{
    RegEntryID      entry;
    RegEntryIter    cookie;
    Boolean         done = false;
    OSStatus        err;
    UInt32          regProp[20];
    RegPropertyValueSize sz;

    err = RegistryEntryIterateCreate(&cookie);
    if (err != noErr) return err;
    err = RegistryEntrySearch(&cookie, kRegIterSubTrees,
                               &entry, &done,
                               "device_type", "mac-io", 7);
    if (err != noErr || done) {
        RegistryEntryIterateDispose(&cookie);
        err = RegistryEntryIterateCreate(&cookie);
        if (err != noErr) return err;
        err = RegistryEntrySearch(&cookie, kRegIterSubTrees,
                                   &entry, &done,
                                   "name", "mac-io", 6);
    }
    RegistryEntryIterateDispose(&cookie);
    if (err != noErr || done) return -1;

    sz = sizeof(regProp);
    err = RegistryPropertyGet(&entry, "assigned-addresses",
                               regProp, &sz);
    if (err != noErr) {
        sz = sizeof(regProp);
        err = RegistryPropertyGet(&entry, "reg", regProp, &sz);
        if (err != noErr) return err;
    }
    gMacIOBase = regProp[2];
    return (gMacIOBase != 0) ? noErr : (OSStatus)-1;
}

/* ============================================================= */
/* On-screen text helpers                                        */
/* ============================================================= */

static void SetItemText(DialogPtr dlg, short item, ConstStr255Param s)
{
    DialogItemType type;
    Handle h;
    Rect box;
    GetDialogItem(dlg, item, &type, &h, &box);
    SetDialogItemText(h, s);
}

static void AppendHex(Str255 dst, const char *label, UInt8 v)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned char *p = dst + 1 + dst[0];
    const char *l = label;
    while (*l) { *p++ = (unsigned char)*l++; dst[0]++; }
    *p++ = '$'; dst[0]++;
    *p++ = (unsigned char)hex[(v >> 4) & 0xF]; dst[0]++;
    *p++ = (unsigned char)hex[v & 0xF]; dst[0]++;
    *p++ = ' '; dst[0]++;
}

static void AppendStr(Str255 dst, const char *s)
{
    unsigned char *p = dst + 1 + dst[0];
    while (*s) { *p++ = (unsigned char)*s++; dst[0]++; }
}

static void AppendHex16(Str255 dst, const char *label, UInt16 v)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned char *p = dst + 1 + dst[0];
    const char *l = label;
    short k;
    while (*l) { *p++ = (unsigned char)*l++; dst[0]++; }
    *p++ = '$'; dst[0]++;
    for (k = 12; k >= 0; k -= 4) {
        *p++ = (unsigned char)hex[(v >> k) & 0xF];
        dst[0]++;
    }
    *p++ = ' '; dst[0]++;
}

/* Tier 3 probe: read the Sound Manager default output volume and show L/R. */
static void DoVolReadback(DialogPtr dlg, const char *prefix)
{
    Str255 s;
    long   vol = 0;
    OSErr  e = GetDefaultOutputVolume(&vol);
    s[0] = 0;
    AppendStr(s, prefix);
    AppendStr(s, "  ");
    if (e != noErr) {
        AppendStr(s, "GetDefaultOutputVolume err ");
        AppendHex16(s, "=", (UInt16)e);
    } else {
        AppendHex16(s, "L=", (UInt16)(vol & 0xFFFF));
        AppendHex16(s, "R=", (UInt16)((vol >> 16) & 0xFFFF));
        AppendStr(s, "(unity=$0100). Tone ON: did loudness change?");
    }
    SetItemText(dlg, kTxtStatus, s);
}

static void AppendOSType(Str255 dst, OSType t)
{
    unsigned char *p = dst + 1 + dst[0];
    short i;
    for (i = 3; i >= 0; i--) { *p++ = (unsigned char)((t >> (i * 8)) & 0xFF); dst[0]++; }
}

static void AppendCR(Str255 dst)
{
    dst[1 + dst[0]] = 0x0D;   /* CR = line break in a dialog StaticText */
    dst[0]++;
}

/* Append one selector's result: " sel=$XXXX" or " sel!err$XXXX". */
static void AppendSel(Str255 s, ComponentInstance ci, const char *name, OSType sel)
{
    long val = 0;
    ComponentResult r = SoundComponentGetInfo(ci, 0, sel, &val);
    AppendStr(s, " ");
    AppendStr(s, name);
    if (r != 0) AppendHex16(s, "!err", (UInt16)r);
    else        AppendHex16(s, "=", (UInt16)val);
}

/* Tier 3 Route-1 premise check: enumerate 'sdev' output components and report
 * whether each claims hardware volume (siHardwareVolumeSteps). READ-ONLY. */
static void DoComponentProbe(DialogPtr dlg)
{
    Str255               s;
    ComponentDescription want, got;
    Component            c = 0;
    short                n = 0;

    s[0] = 0;
    want.componentType         = kSoundOutputDeviceType;
    want.componentSubType      = 0;
    want.componentManufacturer = 0;
    want.componentFlags        = 0;
    want.componentFlagsMask    = 0;

    c = FindNextComponent(0, &want);
    if (c == 0) {
        SetItemText(dlg, kTxtStatus, "\pNo 'sdev' sound output components found.");
        return;
    }

    while (c != 0 && n < 3) {
        ComponentInstance ci = 0;
        n++;
        if (GetComponentInfo(c, &got, 0, 0, 0) == noErr) {
            AppendStr(s, "sdev '");
            AppendOSType(s, got.componentSubType);
            AppendStr(s, "'");
        } else {
            AppendStr(s, "sdev ?");
        }
        {
            OSErr oe = OpenAComponent(c, &ci);
            if (oe == noErr && ci != 0) {
                AppendSel(s, ci, "stp", siHardwareVolumeSteps);
                AppendSel(s, ci, "vol", siHardwareVolume);
                AppendSel(s, ci, "mut", siHardwareMute);
                CloseComponent(ci);
            } else {
                AppendHex16(s, " open!err", (UInt16)oe);
            }
        }
        AppendCR(s);
        c = FindNextComponent(c, &want);
    }
    SetItemText(dlg, kTxtStatus, s);
}

/* ---- Tier 3: read SoftVolINIT's diagnostic counters. The INIT publishes the
 * address of its SVDiag block via a Gestalt selector ('SVdg') — INIT-registered
 * components are invisible to a later app's FindNextComponent, so the old
 * component-refcon channel was a dead end. OS 9 is a single unprotected address
 * space, so dereferencing the INIT's system-heap pointer here is valid.
 * SVDiag layout MUST match init/SoftVolINIT.c. ---- */
#define kDiagGestalt  'SVdg'
#define kDiagMagic    'SVd1'

typedef struct {
    long magic, nOpen, nInit, nSetSource, nGetSrcData, nSetOutput,
         nAddSource, nStart, nPlayBuf, nDeleg, lastWhat, pullErr, tgt;
} SVDiag;

static void DoReadDiag(DialogPtr dlg)
{
    Str255   s;
    long     val = 0;
    SVDiag  *dg;
    OSErr    e;

    s[0] = 0;
    e = Gestalt(kDiagGestalt, &val);
    if (e != noErr || val == 0) {
        AppendStr(s, "no diag (SoftVol INIT not installed?) Gestalt err");
        AppendHex16(s, "=", (UInt16)e);
        SetItemText(dlg, kTxtStatus, s);
        return;
    }
    dg = (SVDiag *)val;
    if (dg->magic != kDiagMagic) {
        AppendStr(s, "bad magic; ptr=");
        AppendHex16(s, "", (UInt16)(val >> 16));
        AppendHex16(s, "", (UInt16)(val & 0xFFFF));
        SetItemText(dlg, kTxtStatus, s);
        return;
    }
    AppendHex16(s, "op", (UInt16)dg->nOpen);
    AppendHex16(s, "in", (UInt16)dg->nInit);
    AppendHex16(s, "ss", (UInt16)dg->nSetSource);
    AppendHex16(s, "gd", (UInt16)dg->nGetSrcData);
    AppendHex16(s, "so", (UInt16)dg->nSetOutput);
    AppendHex16(s, "as", (UInt16)dg->nAddSource);
    AppendHex16(s, "st", (UInt16)dg->nStart);
    AppendHex16(s, "pb", (UInt16)dg->nPlayBuf);
    AppendHex16(s, "dl", (UInt16)dg->nDeleg);
    AppendCR(s);
    AppendHex16(s, "lastW", (UInt16)dg->lastWhat);
    AppendHex16(s, "pullErr", (UInt16)dg->pullErr);
    AppendHex16(s, "tgt", (UInt16)dg->tgt);
    SetItemText(dlg, kTxtStatus, s);
}

/* Delegating wrapper component (local routine). On open it opens the real
 * 'awgc' output component as its TARGET and stashes that instance as its
 * instance storage; every non-handled selector is forwarded to 'awgc' via
 * DelegateComponentCall. This is the production routine minus the PCM scaling
 * (which will intercept kSoundComponentPlaySourceBufferSelect later). */
static pascal ComponentResult WrapRoutine(ComponentParameters *params, Handle storage)
{
    switch ((short)params->what) {
        case kComponentRegisterSelect:
            return 0;

        case kComponentOpenSelect: {
            ComponentInstance self = (ComponentInstance)params->params[0];
            ComponentInstance target = 0;
            gWrapOpens++;
            /* Open the REAL 'awgc' via the capture reference (NOT by description,
             * which would redirect back to us → infinite recursion). */
            if (gCaptureRef != 0)
                OpenAComponent(gCaptureRef, &target);
            SetComponentInstanceStorage(self, (Handle)target);
            return noErr;
        }

        case kComponentCloseSelect:
            if (storage != 0)
                CloseComponent((ComponentInstance)storage);
            return noErr;

        case kComponentCanDoSelect:    return 1;
        case kComponentVersionSelect:  return 0x00010000;
        case kComponentTargetSelect:   return noErr;

        case kSoundComponentPlaySourceBufferSelect:   /* 0x0108 */
            gWrapPlays++;                             /* (Phase 3: scale here) */
            if (storage != 0)
                return DelegateComponentCall(params, (ComponentInstance)storage);
            return badComponentSelector;

        default:
            /* forward everything else to the real 'awgc' */
            if (storage != 0)
                return DelegateComponentCall(params, (ComponentInstance)storage);
            return badComponentSelector;
    }
}

/* Tier 3 insertion experiment (v9): first click CAPTURES 'awgc' (registers our
 * wrapper + CaptureComponent). Then play the Tone and click again to read the
 * counters: if opens>0 the Sound Manager routed through our wrapper => runtime
 * capture inserts us. If opens stays 0, the SM's output is already open and we
 * need a boot-time host. WARNING: if opens>0, REBOOT rather than Quit (tearing
 * down a wrapper the SM is using would crash). */
static void DoFragTest(DialogPtr dlg)
{
    Str255 s;
    s[0] = 0;

    if (gWrapReg == 0) {
        ComponentDescription d, cd;
        Component awgc;
        d.componentType = 'sdev'; d.componentSubType = 'awgc';
        d.componentManufacturer = 0; d.componentFlags = 0; d.componentFlagsMask = 0;
        awgc = FindNextComponent(0, &d);
        if (awgc == 0) { SetItemText(dlg, kTxtStatus, "\pno 'awgc' found"); return; }

        gWrapUPP = NewComponentRoutineUPP(WrapRoutine);
        cd.componentType = 'sdev'; cd.componentSubType = 'SVl3';
        cd.componentManufacturer = 'MmAv'; cd.componentFlags = 0; cd.componentFlagsMask = 0;
        gWrapReg = RegisterComponent(&cd, gWrapUPP, 0, nil, nil, nil);
        if (gWrapReg == 0) { SetItemText(dlg, kTxtStatus, "\pRegisterComponent failed"); return; }

        gWrapOpens = 0; gWrapPlays = 0;
        gAwgcComp = awgc;
        gCaptureRef = CaptureComponent(awgc, gWrapReg);
        AppendStr(s, "v9 CAPTURED awgc. Now: Tone On, let it play, then click ");
        AppendStr(s, "this again. If opens>0, REBOOT (do not Quit).");
    } else {
        AppendStr(s, "v9 counts: opens");
        AppendHex16(s, "=", (UInt16)gWrapOpens);
        AppendHex16(s, " plays=", (UInt16)gWrapPlays);
        if (gWrapOpens > 0)
            AppendStr(s, " INSERTED! (reboot, don't quit)");
        else
            AppendStr(s, " not inserted (SM output already open -> need boot host)");
    }
    SetItemText(dlg, kTxtStatus, s);
}

/* ============================================================= */
/* Amp control                                                   */
/* ============================================================= */

static void SetAllAmps(UInt8 value)
{
    WriteGPIO(GPIO_AMP_1, value);
    WriteGPIO(GPIO_AMP_2, value);
    WriteGPIO(GPIO_AMP_3, value);
    WriteGPIO(GPIO_AMP_4, value);
}

static void SetAmps(UInt8 v6E, UInt8 v6F, UInt8 v70, UInt8 v79)
{
    WriteGPIO(GPIO_AMP_1, v6E);
    WriteGPIO(GPIO_AMP_2, v6F);
    WriteGPIO(GPIO_AMP_3, v70);
    WriteGPIO(GPIO_AMP_4, v79);
}

static void DoReadback(DialogPtr dlg, const char *prefix)
{
    Str255 s;
    s[0] = 0;
    AppendStr(s, prefix);
    AppendStr(s, "  ");
    AppendHex(s, "6D=", ReadGPIO(GPIO_AMP_X));
    AppendHex(s, "6E=", ReadGPIO(GPIO_AMP_1));
    AppendHex(s, "6F=", ReadGPIO(GPIO_AMP_2));
    AppendHex(s, "70=", ReadGPIO(GPIO_AMP_3));
    AppendHex(s, "79=", ReadGPIO(GPIO_AMP_4));
    AppendHex(s, "67=", ReadGPIO(GPIO_HP_DETECT));
    AppendHex(s, "51=", ReadGPIO(GPIO_HP_ALT));
    SetItemText(dlg, kTxtStatus, s);
}

/* Tier 2 auto-switch: poll HP detect, route on change. Main-thread only. */
static void PollAndSwitch(DialogPtr dlg)
{
    UInt8 d;
    if (!gAutoOn) return;
    d = ReadGPIO(GPIO_HP_DETECT) & 0x02;   /* bit 1: 1=no HP, 0=HP in */
    if (d == gLastDetect) return;
    gLastDetect = d;
    if (d == 0) {                          /* headphones inserted */
        WriteGPIO(GPIO_AMP_2, AMP_ON);     /* 6F headphones on  */
        WriteGPIO(GPIO_AMP_3, AMP_STANDBY);/* 70 speaker muted  */
        SetItemText(dlg, kTxtStatus,
            "\pAUTO: headphones in -> 6F on, 70 (speaker) muted.");
    } else {                               /* no headphones */
        WriteGPIO(GPIO_AMP_3, AMP_ON);     /* 70 speaker on     */
        WriteGPIO(GPIO_AMP_2, AMP_STANDBY);/* 6F headphones off */
        SetItemText(dlg, kTxtStatus,
            "\pAUTO: no headphones -> 70 (speaker) on, 6F muted.");
    }
}

/* ============================================================= */
/* Self-contained async test tone (looping square wave)         */
/* ============================================================= */

#define kToneSamples 50          /* ~441 Hz at 22 kHz, full loop */

static struct {
    Ptr           samplePtr;
    unsigned long length;
    UnsignedFixed sampleRate;
    unsigned long loopStart;
    unsigned long loopEnd;
    UInt8         encode;
    UInt8         baseFrequency;
    UInt8         samples[kToneSamples];
} gTone;

/* Square-wave amplitude (0..0x7F) around the 0x80 midpoint. Default 0x40 =
 * half scale. Tier 3 Route-2 go/no-go: lower amplitude == scaled PCM. */
static UInt8 gAmp = 0x40;

static void BuildTone(void)
{
    short i;
    gTone.samplePtr     = nil;
    gTone.length        = kToneSamples;
    gTone.sampleRate    = 0x56EE8BA3;   /* rate22khz */
    gTone.loopStart     = 0;
    gTone.loopEnd       = kToneSamples;
    gTone.encode        = 0x00;         /* stdSH */
    gTone.baseFrequency = 60;           /* kMiddleC */
    for (i = 0; i < kToneSamples; i++)
        gTone.samples[i] = (i < kToneSamples / 2)
                            ? (UInt8)(0x80 + gAmp)
                            : (UInt8)(0x80 - gAmp);
}

static void StartTone(void)
{
    SndCommand cmd;
    if (gToneChan != nil) return;
    if (SndNewChannel(&gToneChan, sampledSynth, initMono, nil) != noErr) {
        gToneChan = nil;
        return;
    }
    cmd.cmd    = bufferCmd;
    cmd.param1 = 0;
    cmd.param2 = (long)&gTone;
    SndDoCommand(gToneChan, &cmd, false);
}

static void StopTone(void)
{
    if (gToneChan == nil) return;
    SndDisposeChannel(gToneChan, true);
    gToneChan = nil;
}

/* Tier 3 go/no-go: rebuild the tone buffer at a new amplitude (scaled PCM) and
 * restart it. If the DAC faithfully reproduces amplitude, lower amplitude is
 * audibly quieter — which proves software PCM scaling (Route 2) is viable. */
static void DoAmp(DialogPtr dlg, UInt8 amp, const char *prefix)
{
    Boolean wasOn = (gToneChan != nil);
    Str255  s;
    gAmp = amp;
    BuildTone();
    if (wasOn) { StopTone(); StartTone(); }
    s[0] = 0;
    AppendStr(s, prefix);
    if (!wasOn) AppendStr(s, " (Tone is OFF — turn it ON to hear it.)");
    else        AppendStr(s, " scaled-PCM tone playing. Quieter than before?");
    SetItemText(dlg, kTxtStatus, s);
}

/* ============================================================= */
/* Item dispatch                                                 */
/* ============================================================= */

static void HandleItem(DialogPtr dlg, short item)
{
    switch (item) {
    /* ---- Tier 1: all-amp states ---- */
    case kBtnAmpsOn:
        gAutoOn = false;
        SetAllAmps(AMP_ON);
        DoReadback(dlg, "Amps ON ($05):");
        break;
    case kBtnMute:
        gAutoOn = false;
        SetAllAmps(AMP_MUTE);
        DoReadback(dlg, "MUTE ($01):");
        break;
    case kBtnStandby:
        gAutoOn = false;
        SetAllAmps(AMP_STANDBY);
        DoReadback(dlg, "Standby ($04):");
        break;
    case kBtnRead:
        DoReadback(dlg, "Readback:");
        break;
    case kBtnTone:
        if (gToneChan == nil) {
            StartTone();
            SetItemText(dlg, kTxtStatus,
                "\pTone ON. Turn Auto-switch on, then plug/unplug headphones.");
        } else {
            StopTone();
            SetItemText(dlg, kTxtStatus, "\pTone OFF.");
        }
        break;

    /* ---- Tier 2: auto-switch ---- */
    case kBtnAuto:
        gAutoOn = !gAutoOn;
        if (gAutoOn) {
            gLastDetect = 0xFF;            /* force immediate apply */
            PollAndSwitch(dlg);
        } else {
            SetAllAmps(AMP_ON);
            SetItemText(dlg, kTxtStatus,
                "\pAuto-switch OFF. Both outputs ON.");
        }
        break;

    /* ---- Tier 2: routing isolation (one group ON, rest STANDBY) ---- */
    case kBtnGrp123:
        gAutoOn = false;
        SetAmps(AMP_ON, AMP_ON, AMP_ON, AMP_STANDBY);
        DoReadback(dlg, "ONLY 6E+6F+70:");
        break;
    case kBtnOnly79:
        gAutoOn = false;
        SetAmps(AMP_STANDBY, AMP_STANDBY, AMP_STANDBY, AMP_ON);
        DoReadback(dlg, "ONLY 79:");
        break;
    case kBtnOnly6E:
        gAutoOn = false;
        SetAmps(AMP_ON, AMP_STANDBY, AMP_STANDBY, AMP_STANDBY);
        DoReadback(dlg, "ONLY 6E:");
        break;
    case kBtnOnly6F:
        gAutoOn = false;
        SetAmps(AMP_STANDBY, AMP_ON, AMP_STANDBY, AMP_STANDBY);
        DoReadback(dlg, "ONLY 6F (headphones):");
        break;
    case kBtnOnly70:
        gAutoOn = false;
        SetAmps(AMP_STANDBY, AMP_STANDBY, AMP_ON, AMP_STANDBY);
        DoReadback(dlg, "ONLY 70 (speaker):");
        break;

    /* ---- Tier 3: software volume probe (Sound Manager mixer) ---- */
    case kBtnVolRead:
        DoVolReadback(dlg, "DefOutVol:");
        break;
    case kBtnVol100:
        DoAmp(dlg, 0x7F, "Amp 100%:");
        break;
    case kBtnVol50:
        DoAmp(dlg, 0x40, "Amp 50%:");
        break;
    case kBtnVol25:
        DoAmp(dlg, 0x20, "Amp 25%:");
        break;
    case kBtnVol0:
        DoAmp(dlg, 0x00, "Amp 0%:");
        break;
    case kBtnProbe:
        DoComponentProbe(dlg);
        break;
    case kBtnDiag:
        DoReadDiag(dlg);
        break;
    }
}

/* ============================================================= */
/* Main                                                          */
/* ============================================================= */

int main(void)
{
    DialogPtr   dlg;
    OSStatus    err;
    long        gestaltResult;
    Boolean     haveBase = false;
    Boolean     quitting = false;
    EventRecord evt;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    BuildTone();

    dlg = GetNewDialog(128, 0, (WindowPtr)-1);
    if (dlg == nil) return 1;

    err = Gestalt(gestaltNameRegistryVersion, &gestaltResult);
    if (err == noErr)
        err = FindIOControllerBase();

    if (err == noErr && gMacIOBase != 0) {
        Str255 t;
        static const char hex[] = "0123456789ABCDEF";
        short k;
        t[0] = 0;
        AppendStr(t, "mac-io base = $");
        for (k = 28; k >= 0; k -= 4) {
            unsigned char *p = t + 1 + t[0];
            *p = (unsigned char)hex[(gMacIOBase >> k) & 0xF];
            t[0]++;
        }
        SetItemText(dlg, kTxtTitle, t);
        haveBase = true;
        DoReadback(dlg, "Boot state:");
    } else {
        SetItemText(dlg, kTxtTitle,
            "\pFAILED to find mac-io base — GPIO disabled. NOT this machine?");
        SetItemText(dlg, kTxtStatus,
            "\pName Registry lookup failed. GPIO buttons are unsafe; quit.");
    }

    ShowWindow(GetDialogWindow(dlg));

    /* Modeless event loop so we can poll HP-detect between clicks. */
    while (!quitting) {
        WaitNextEvent(everyEvent, &evt, kSleepTicks, nil);

        if (haveBase)
            PollAndSwitch(dlg);

        if (IsDialogEvent(&evt)) {
            DialogPtr whichDlg;
            short     itemHit;
            if (DialogSelect(&evt, &whichDlg, &itemHit)) {
                if (itemHit == kBtnQuit)
                    quitting = true;
                else if (haveBase)
                    HandleItem(dlg, itemHit);
            }
        }
    }

    StopTone();
    /* Tier 3 experiment teardown: restore 'awgc' so the captured wrapper (whose
     * code dies with this app) can't dangle. Safe when the SM never opened us
     * (gWrapOpens==0); if it DID, the user was told to reboot, not quit. */
    if (gWrapReg != 0) {
        if (gAwgcComp != 0) UncaptureComponent(gAwgcComp);
        UnregisterComponent(gWrapReg);
        if (gWrapUPP != 0) DisposeComponentRoutineUPP(gWrapUPP);
    }
    SetAllAmps(AMP_ON);          /* leave audio working on exit */
    DisposeDialog(dlg);
    FlushEvents(everyEvent, -1);
    return 0;
}
