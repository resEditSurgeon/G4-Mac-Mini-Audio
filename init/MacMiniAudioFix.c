/*
 *  MacMiniAudioFix.c  —  v9.0  (Tier 1: factored amp states + mute path)
 *
 *  Mac OS 9 System Extension for Mac Mini G4 (A1103, spoofed PowerMac5,1).
 *  Enables the output amplifier GPIOs that the screamer driver leaves
 *  disabled, restoring full-volume internal speaker + headphone audio.
 *
 *  CHANGES FROM v8 (the working release):
 *    - Amp control factored into named-state helpers (SetAllAmps / amp
 *      state constants) and a documented mute path (MuteAmps/StandbyAmps),
 *      so Tier 2's resident trigger can call them without restructuring.
 *    - Compile-time tier toggles below.
 *    - BOOT BEHAVIOR IS UNCHANGED: amps are driven to AMP_ON at startup,
 *      exactly like v8. This file does not regress the working fix.
 *
 *  NOTE ON MUTE (Tier 1):
 *    The mute *mechanism* (park amps at AMP_MUTE = 0x01) is validated
 *    interactively by tools/MiniAudioTool before it is wired to a trigger.
 *    An INIT runs once at boot, so a runtime mute toggle needs a resident
 *    component (the Tier 2 Time Manager task). Until that lands, this INIT
 *    only guarantees the known-good boot state; the mute helpers are
 *    compiled in and ready but not yet invoked at runtime.
 *
 *  Build: CodeWarrior, PPC Code Resource, INIT ID 0, File/Creator INIT/MmAf
 *  Link:  InterfaceLib, NameRegistryLib, DriverServicesLib
 *  (Retro68 cannot emit a PowerPC INIT code resource — see README.)
 */

#include <Types.h>
#include <Memory.h>
#include <Resources.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <LowMem.h>
#include <NameRegistry.h>
#include <DriverServices.h>
#include <Icons.h>

/* ===== Compile-time tier toggles =================================== */
/* Tier 1 is the v8 boot behavior. Leave at 1 unless bisecting.        */
#define TIER1_ENABLE_AMPS_AT_BOOT   1
/* Tier 2 (resident HP auto-switch) lands in a later round.            */
#define TIER2_RESIDENT_SWITCH       0
/* =================================================================== */

static OSStatus FindIOControllerBase(void);
static void     ShowINITIcon(short iconID, Boolean advance);

#define kIconOK_ID      128
#define kIconFail_ID    129
#define kIconSkip_ID    130

static UInt32   gMacIOBase = 0;

/* GPIO pin assignments (Mac Mini G4 Intrepid) */
#define GPIO_HP_DETECT  0x67    /* bit 1: 1=no HP, 0=HP inserted */
#define GPIO_AMP_1      0x6E    /* Amp enable */
#define GPIO_AMP_2      0x6F    /* Amp enable */
#define GPIO_AMP_3      0x70    /* Amp enable */
#define GPIO_AMP_4      0x79    /* Amp enable (master?) */

/* Amp pin states (see TECHNICAL.txt GPIO bit layout) */
#define AMP_ON          0x05    /* output enable + data HIGH = ON      */
#define AMP_MUTE        0x01    /* output enable + data LOW  = MUTED   */
#define AMP_STANDBY     0x04    /* output disabled + data HIGH = OFF   */

/* ---- GPIO access ---- */

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

/* ---- Amp state helpers (shared with the resident task in Tier 2) ---- */

static void SetAllAmps(UInt8 value)
{
    WriteGPIO(GPIO_AMP_1, value);   /* 0x6E */
    WriteGPIO(GPIO_AMP_2, value);   /* 0x6F */
    WriteGPIO(GPIO_AMP_3, value);   /* 0x70 */
    WriteGPIO(GPIO_AMP_4, value);   /* 0x79 */
}

static void EnableAmps(void)  { SetAllAmps(AMP_ON); }
static void MuteAmps(void)    { SetAllAmps(AMP_MUTE); }
static void StandbyAmps(void) { SetAllAmps(AMP_STANDBY); }

/* ---- Find mac-io ---- */

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

/* ---- INIT Icon ---- */

static void ShowINITIcon(short iconID, Boolean advance)
{
    Handle      iconHandle;
    short       hPos;
    Rect        iconRect;
    GrafPort    myPort;
    GrafPtr     savePort;

    iconHandle = GetResource('ICN#', iconID);
    if (iconHandle == nil) return;
    hPos = *(short *)0x92C;
    if (hPos == 0) hPos = 8;
    GetPort(&savePort);
    OpenPort(&myPort);
    iconRect.left   = hPos;
    iconRect.right  = hPos + 32;
    iconRect.bottom = myPort.portBits.bounds.bottom - 8;
    iconRect.top    = iconRect.bottom - 32;
    PlotIconID(&iconRect, atNone, ttNone, iconID);
    if (advance) {
        hPos += 40;
        *(short *)0x92C = hPos;
    }
    ClosePort(&myPort);
    SetPort(savePort);
    ReleaseResource(iconHandle);
}

/* ---- Main ---- */

void main(void)
{
    OSStatus err;
    long gestaltResult;

    err = Gestalt(gestaltNameRegistryVersion, &gestaltResult);
    if (err != noErr) {
        ShowINITIcon(kIconFail_ID, true);
        return;
    }

    err = FindIOControllerBase();
    if (err != noErr || gMacIOBase == 0) {
        ShowINITIcon(kIconFail_ID, true);
        return;
    }

    /*
     * Drive all amplifier GPIO pins to AMP_ON.
     *
     * After the screamer driver loads, these pins sit at 0x04 (data HIGH
     * but output disabled). Writing AMP_ON (0x05 = output enable + data
     * HIGH) re-enables them; hardware reads back 0x07.
     *
     * FAIL-SAFE: this is the known-good v8 behavior. If a later tier's
     * extra logic is disabled or fails, the machine still boots with
     * full-volume audio.
     */
#if TIER1_ENABLE_AMPS_AT_BOOT
    EnableAmps();
    ShowINITIcon(kIconOK_ID, true);
#else
    ShowINITIcon(kIconSkip_ID, true);
#endif

    /* Silence "unused function" warnings until Tier 2 wires the triggers. */
    (void)MuteAmps;
    (void)StandbyAmps;
    (void)ReadGPIO;
}
