#!/bin/sh
# Build the Retro68 deliverables: the Mini Audio Control Strip module and the
# MiniAudioTool diagnostic app. Produces mountable .img disk images (+ MacBinary
# .bin fallbacks) and stages them in prebuilt/.
#
# Requires Retro68 (https://github.com/autc04/Retro68) built for PowerPC.
# Set RETRO68 to your Retro68-build directory if it isn't ~/Retro68-build.
#
# NOTE: the amp-enable INIT (init/) is NOT built here — Retro68 cannot emit a
# resident PowerPC INIT code resource; build it with CodeWarrior on the G4
# (see init/README.md).
set -e

RETRO68="${RETRO68:-$HOME/Retro68-build}"
TOOLCHAIN="$RETRO68/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"
HERE="$(cd "$(dirname "$0")" && pwd)"
WRAP="$HERE/scripts/wrap-apm.py"

if [ ! -f "$TOOLCHAIN" ]; then
    echo "ERROR: Retro68 PPC toolchain not found at: $TOOLCHAIN" >&2
    echo "Set RETRO68 to your Retro68-build directory." >&2
    exit 1
fi

mkdir -p "$HERE/prebuilt"

# ---- Control Strip module (self-wraps to .img via scripts/wrap-apm.py) -------
CSM="$HERE/csm/mini-audio-strip"
rm -rf "$CSM/build"
cmake -S "$CSM" -B "$CSM/build" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
cmake --build "$CSM/build"
cp -f "$CSM/build/MiniAudioStrip.img" "$HERE/prebuilt/"
cp -f "$CSM/build/MiniAudioStrip.bin" "$HERE/prebuilt/"

# ---- MiniAudioTool app (Retro68 emits .dsk; wrap to .img here) ---------------
APP="$HERE/tools/MiniAudioTool"
rm -rf "$APP/build"
cmake -S "$APP" -B "$APP/build" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
cmake --build "$APP/build"
cp -f "$APP/build/MiniAudioTool.bin" "$HERE/prebuilt/"
python3 "$WRAP" "$APP/build/MiniAudioTool.dsk" "$HERE/prebuilt/MiniAudioTool.img" MiniAudioTool

echo ""
echo "Built into prebuilt/:"
echo "  MiniAudioStrip.img  <- install into System Folder:Control Strip Modules:"
echo "  MiniAudioTool.img   <- the optional diagnostic app"
echo "  *.bin               <- MacBinary fallbacks (decode with StuffIt)"
