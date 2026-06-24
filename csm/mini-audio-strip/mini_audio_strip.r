#include "MixedMode.r"

/*
 * 'sdev' code resource = a Mixed Mode routine descriptor wrapping our PPC PEF.
 * ProcInfo for the Control Strip entry point:
 *     pascal long MiniAudioStrip(long message, long params,
 *                                Rect *statusRect, GrafPtr statusPort)
 *   kPascalStackBased            = 0x0000
 *   result  long   (4) -> 3<<4   = 0x0030
 *   param1  long   (4) -> 3<<6   = 0x00C0
 *   param2  long   (4) -> 3<<8   = 0x0300
 *   param3  Rect*  (4) -> 3<<10  = 0x0C00
 *   param4  GrafPtr(4) -> 3<<12  = 0x3000
 *                                 --------
 *                       ProcInfo = 0x00003FF0
 * (Same signature as the working UT-launcher CSM.)
 */
type 'sdev' as 'rdes';

resource 'sdev' (128, "Mini Audio", locked) {
    0x00003FF0,
    $$Read("MiniAudioStrip.pef")
};
