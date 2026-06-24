/*
 *  MiniAudioTool.r  —  v4 (Tier 1 mute + Tier 2 routing/auto-switch + Tier 3 volume probe)
 *
 *  DITL item order MUST match the #defines in MiniAudioTool.c:
 *    1 Amps ON  2 MUTE  3 Standby  4 Read GPIO  5 Tone  6 Quit
 *    7 Auto-switch
 *    8 ONLY 6E+6F+70  9 ONLY 79  10 ONLY 6E  11 ONLY 6F  12 ONLY 70
 *    13 title text   14 status text   15 routing label
 *    16 Read Vol  17 Vol 100%  18 Vol 50%  19 Vol 25%  20 Vol 0%
 *    21 volume label
 */

#include "Dialogs.r"

resource 'DLOG' (128) {
    { 6, 80, 516, 560 },         /* {top,left,bottom,right} : 510 x 480 */
    dBoxProc,
    visible,
    noGoAway,
    0,
    128,
    "MiniAudioTool",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        /* 1: Amps ON (default button = safe restore on Return) */
        { 10, 10, 34, 160 },
        Button { enabled, "Amps ON ($05)" };

        /* 2: MUTE */
        { 10, 170, 34, 320 },
        Button { enabled, "MUTE ($01)" };

        /* 3: Standby */
        { 10, 330, 34, 470 },
        Button { enabled, "Standby ($04)" };

        /* 4: Read GPIO */
        { 44, 10, 68, 160 },
        Button { enabled, "Read GPIO" };

        /* 5: Tone on/off */
        { 44, 170, 68, 320 },
        Button { enabled, "Tone On/Off" };

        /* 6: Quit */
        { 44, 330, 68, 470 },
        Button { enabled, "Quit" };

        /* 7: Auto-switch (full width) */
        { 80, 10, 104, 470 },
        Button { enabled, "Auto-switch headphones <-> speaker (ON/OFF)" };

        /* 8: routing — 6E+6F+70 ON, 79 OFF */
        { 134, 10, 158, 230 },
        Button { enabled, "ONLY 6E+6F+70" };

        /* 9: routing — 79 ON, rest OFF */
        { 134, 240, 158, 470 },
        Button { enabled, "ONLY 79" };

        /* 10: routing — 6E only */
        { 164, 10, 188, 160 },
        Button { enabled, "ONLY 6E" };

        /* 11: routing — 6F only (headphones) */
        { 164, 170, 188, 320 },
        Button { enabled, "ONLY 6F" };

        /* 12: routing — 70 only (speaker) */
        { 164, 330, 188, 470 },
        Button { enabled, "ONLY 70" };

        /* 13: title / base-address line */
        { 286, 10, 306, 470 },
        StaticText { disabled, "Probing Name Registry..." };

        /* 14: status / readback (multi-line) */
        { 312, 10, 504, 470 },
        StaticText { disabled, "" };

        /* 15: section label for the routing row */
        { 116, 10, 132, 470 },
        StaticText { disabled,
            "Manual routing test (Tone ON; check speaker AND headphones):" };

        /* 16: Read default output volume */
        { 216, 10, 240, 100 },
        Button { enabled, "Read Vol" };

        /* 17: Vol 100% */
        { 216, 108, 240, 196 },
        Button { enabled, "Vol 100%" };

        /* 18: Vol 50% */
        { 216, 204, 240, 292 },
        Button { enabled, "Vol 50%" };

        /* 19: Vol 25% */
        { 216, 300, 240, 388 },
        Button { enabled, "Vol 25%" };

        /* 20: Vol 0% */
        { 216, 396, 240, 470 },
        Button { enabled, "Vol 0%" };

        /* 21: section label for the volume row */
        { 198, 10, 214, 470 },
        StaticText { disabled,
            "Tier 3: scaled-PCM amplitude test (Tone ON) - does loudness change?" };

        /* 22: probe sound components */
        { 250, 10, 274, 300 },
        Button { enabled, "Probe Sound Components" };

        /* 23: reboot-free CFM fragment-load test (needs a 'SoftVol' file beside the app) */
        { 250, 310, 274, 470 },
        Button { enabled, "Read SoftVol diag" };
    }
};

#include "Processes.r"

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    200 * 1024,
    200 * 1024
};
