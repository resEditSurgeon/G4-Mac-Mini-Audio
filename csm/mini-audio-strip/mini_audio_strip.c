/*
 * Mini Audio — a Control Strip Module ('sdev') for the Mac Mini G4 audio fix.
 *
 * Shows the current output in the Control Strip and gives a one-click mute:
 *     "Spk"  internal speaker active     "Hph"  headphone jack active
 *     "Mut"  muted                       "??"   mac-io not found (inert)
 *
 *   - sdevPeriodicTickle polls the headphone-detect GPIO (0x67 bit 1) and
 *     routes audio: headphones in -> 6F on / 70 standby; out -> 70 on / 6F
 *     standby. (6F = headphone jack, 70 = internal speaker; confirmed on
 *     hardware 2026-06-22.)
 *   - sdevMouseClick toggles mute (both outputs -> AMP_MUTE $01), instantly,
 *     independent of the tickle rate.
 *
 * This LAYERS ON TOP of the v8 boot-time INIT, which still drives the amps on
 * at startup. If this module is absent or fails, audio still works (just no
 * switching/mute) — fail-safe by construction.
 *
 * Built as a native PowerPC code resource (PEF) wrapped in a Mixed Mode
 * routine descriptor; the Control Strip calls our entry point through Mixed
 * Mode using the ProcInfo in the .r. CFM sets up the TOC, so calls and
 * read-only data work — but MUTABLE FRAGMENT GLOBALS ARE NOT SAFE in a
 * Control-Strip-loaded code resource (they crash the whole strip). All
 * per-instance state lives in a heap block returned from sdevInitModule as the
 * refCon and handed back as `params` on every later call.
 *
 * Entry ABI (verified vs Apple patent US6493002; not in ControlStrip.h):
 *   pascal long main(long message, long params, Rect *statusRect, GrafPtr port)
 */

#include <ControlStrip.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacTypes.h>
#include <NameRegistry.h>
#include <DriverServices.h>

/* No prototypes for these in this Retro68 interface set; declare the
 * InterfaceLib imports ourselves (pascal is a no-op on PPC/CFM). */
extern pascal Ptr  NewPtrSys(long byteCount);
extern pascal void DisposePtr(Ptr p);

/* ---- GPIO pins (Mac Mini G4 Intrepid; confirmed) ---- */
#define GPIO_HP_DETECT  0x67    /* bit 1: 1=no HP, 0=HP inserted */
#define GPIO_HP         0x6F    /* headphone jack  */
#define GPIO_SPK        0x70    /* internal speaker */

#define AMP_ON          0x05    /* output enable + data HIGH = ON   */
#define AMP_MUTE        0x01    /* output enable + data LOW  = MUTE */
#define AMP_STANDBY     0x04    /* output disabled + data HIGH = OFF*/

#define kCellPad        6

/* Per-instance state, held in the refCon (NEVER fragment globals). */
typedef struct {
    UInt32  base;        /* mac-io base, 0 if not found */
    UInt8   lastDetect;  /* last (0x67 & 0x02) applied  */
    Boolean muted;
} AudioState;

/* ============================================================= */
/* GPIO access — base passed in (no globals)                     */
/* ============================================================= */

static UInt8 RG(UInt32 base, UInt32 off)
{
    volatile UInt8 *a = (volatile UInt8 *)(base + off);
    UInt8 v = *a;
    SynchronizeIO();
    return v;
}

static void WG(UInt32 base, UInt32 off, UInt8 v)
{
    volatile UInt8 *a = (volatile UInt8 *)(base + off);
    *a = v;
    SynchronizeIO();
}

/* ============================================================= */
/* Name Registry mac-io discovery (same logic as the v8 INIT)    */
/* ============================================================= */

static UInt32 FindMacIOBase(void)
{
    RegEntryID      entry;
    RegEntryIter    cookie;
    Boolean         done = false;
    OSStatus        err;
    UInt32          regProp[20];
    RegPropertyValueSize sz;

    if (RegistryEntryIterateCreate(&cookie) != noErr)
        return 0;
    err = RegistryEntrySearch(&cookie, kRegIterSubTrees, &entry, &done,
                              "device_type", "mac-io", 7);
    if (err != noErr || done) {
        RegistryEntryIterateDispose(&cookie);
        if (RegistryEntryIterateCreate(&cookie) != noErr)
            return 0;
        err = RegistryEntrySearch(&cookie, kRegIterSubTrees, &entry, &done,
                                  "name", "mac-io", 6);
    }
    RegistryEntryIterateDispose(&cookie);
    if (err != noErr || done)
        return 0;

    sz = sizeof(regProp);
    if (RegistryPropertyGet(&entry, "assigned-addresses", regProp, &sz) != noErr) {
        sz = sizeof(regProp);
        if (RegistryPropertyGet(&entry, "reg", regProp, &sz) != noErr)
            return 0;
    }
    return regProp[2];
}

/* ============================================================= */
/* Routing / mute                                                */
/* ============================================================= */

/* Apply speaker-vs-headphone routing from the current detect pin. */
static void ApplyRouting(AudioState *st)
{
    UInt8 d = RG(st->base, GPIO_HP_DETECT) & 0x02;   /* 1=no HP, 0=HP in */
    st->lastDetect = d;
    if (d == 0) {                       /* headphones inserted */
        WG(st->base, GPIO_HP,  AMP_ON);
        WG(st->base, GPIO_SPK, AMP_STANDBY);
    } else {                            /* no headphones */
        WG(st->base, GPIO_SPK, AMP_ON);
        WG(st->base, GPIO_HP,  AMP_STANDBY);
    }
}

static void ApplyMute(AudioState *st)
{
    WG(st->base, GPIO_HP,  AMP_MUTE);
    WG(st->base, GPIO_SPK, AMP_MUTE);
}

/* Compose the 3-char strip label into caller storage (no globals). */
static void BuildLabel(AudioState *st, StringPtr out)
{
    const char *s;
    if (st == NULL || st->base == 0) s = "??";
    else if (st->muted)              s = "Mut";
    else if (st->lastDetect == 0)    s = "Hph";
    else                             s = "Spk";
    out[0] = 0;
    while (*s) { out[1 + out[0]] = (unsigned char)*s++; out[0]++; }
}

/* ============================================================= */
/* Strip drawing (modeled on the working UT-launcher module)     */
/* ============================================================= */

static void SetupStripFont(GrafPtr port)
{
    short fontID = 0, fontSize = 0;
    if (port != NULL) SetPort(port);
    if (SBGetControlStripFontID(&fontID) == noErr) TextFont(fontID);
    if (SBGetControlStripFontSize(&fontSize) == noErr) TextSize(fontSize);
    TextFace(bold);
}

static void DrawCenteredText(GrafPtr port, const Rect *cell, StringPtr s)
{
    FontInfo fi;
    short cellH, cellW, textW, baseline;
    SetupStripFont(port);
    GetFontInfo(&fi);
    cellH = cell->bottom - cell->top;
    cellW = cell->right - cell->left;
    textW = StringWidth(s);
    baseline = cell->top + (cellH - (fi.ascent + fi.descent)) / 2 + fi.ascent;
    MoveTo(cell->left + (cellW - textW) / 2, baseline);
    DrawString(s);
}

/* ============================================================= */
/* Control Strip entry point                                     */
/* ============================================================= */

pascal long MiniAudioStrip(long message, long params,
                           Rect *statusRect, GrafPtr statusPort)
{
    AudioState *st = (AudioState *)params;   /* refCon, valid after init */

    switch (message) {
        case sdevInitModule: {
            AudioState *newst = (AudioState *)NewPtrSys(sizeof(AudioState));
            if (newst != NULL) {
                newst->base = FindMacIOBase();
                newst->muted = false;
                newst->lastDetect = 0xFF;          /* force first apply */
                if (newst->base != 0)
                    ApplyRouting(newst);
            }
            return (long)newst;
        }

        case sdevCloseModule:
            if (st != NULL) {
                if (st->base != 0) {               /* leave both outputs on */
                    WG(st->base, GPIO_HP,  AMP_ON);
                    WG(st->base, GPIO_SPK, AMP_ON);
                }
                DisposePtr((Ptr)st);
            }
            return 0;

        case sdevFeatures:
            return (1L << sdevWantMouseClicks);

        case sdevGetDisplayWidth: {
            Str255 label;
            SetupStripFont(statusPort);
            BuildLabel(st, label);
            return StringWidth(label) + kCellPad;
        }

        case sdevPeriodicTickle:
            if (st != NULL && st->base != 0 && !st->muted) {
                UInt8 d = RG(st->base, GPIO_HP_DETECT) & 0x02;
                if (d != st->lastDetect) {
                    ApplyRouting(st);
                    return (1L << sdevResizeDisplay);   /* redraw Spk<->Hph */
                }
            }
            return 0;

        case sdevDrawStatus: {
            Str255 label;
            BuildLabel(st, label);
            DrawCenteredText(statusPort, statusRect, label);
            return 0;
        }

        case sdevMouseClick:
            if (st != NULL && st->base != 0) {
                st->muted = !st->muted;
                if (st->muted) ApplyMute(st);
                else           ApplyRouting(st);
            }
            return (1L << sdevResizeDisplay);          /* redraw label */

        case sdevSaveSettings:
            return 0;

        case sdevShowBalloonHelp:
            return 0;
    }
    return 0;
}
