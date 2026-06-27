cmake_minimum_required(VERSION 3.13)
project(MiniAudioStrip C)

# Configure with the Retro68 PowerPC toolchain:
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=$HOME/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   cmake --build build

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
add_compile_options(-Wno-multichar -Wall)

# A Control Strip module is a code resource, NOT an app: plain add_executable
# post-processed into a PEF (modeled on the working UT-launcher CSM).
add_executable(MiniAudioStrip mini_audio_strip.c)

if(TARGET retrocrt)
    add_dependencies(MiniAudioStrip retrocrt)
endif()

# -e<entry> makes our dispatch routine the code resource entry point.
set_target_properties(MiniAudioStrip PROPERTIES
    COMPILE_OPTIONS -ffunction-sections
    LINK_FLAGS "-Wl,-eMiniAudioStrip -Wl,-gc-sections")

# SBGetControlStripFontID/Size -> ControlStripLib; mac-io discovery -> Name
# Registry + DriverServices (SynchronizeIO); everything else -> InterfaceLib.
target_link_libraries(MiniAudioStrip ControlStripLib NameRegistryLib DriverServicesLib)

# XCOFF -> PEF
add_custom_command(
    OUTPUT MiniAudioStrip.pef
    COMMAND ${MAKE_PEF} MiniAudioStrip -o MiniAudioStrip.pef
    DEPENDS MiniAudioStrip)

if(REZ_INCLUDE_PATH)
    set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()

# Rez the PEF into the 'sdev' resource; stamp file type 'sdev' (REQUIRED for the
# Control Strip to load it) + creator. NB: do NOT add "--cc MiniAudioStrip" —
# Rez would clobber the linker's XCOFF of the same name and re-trigger MakePEF on
# a corrupted file (assertion crash). Only emit the .bin and .dsk.
add_custom_command(
    OUTPUT MiniAudioStrip.bin MiniAudioStrip.dsk
    COMMAND ${REZ} ${REZ_INCLUDE_FLAG}
                    -I${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/mini_audio_strip.r
                    -o MiniAudioStrip.bin
                    --cc MiniAudioStrip.dsk
                    -t sdev
                    -c MmAc
    DEPENDS MiniAudioStrip.pef mini_audio_strip.r mini_audio_strip_icons.r)
add_custom_target(MiniAudioStrip_sdev ALL DEPENDS MiniAudioStrip.dsk)

# Wrap the raw HFS .dsk in an Apple Partition Map so OS 9 can mount it.
set(WRAP_APM ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/wrap-apm.py)
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/MiniAudioStrip.img
    COMMAND /usr/bin/env python3 ${WRAP_APM}
            ${CMAKE_CURRENT_BINARY_DIR}/MiniAudioStrip.dsk
            ${CMAKE_CURRENT_BINARY_DIR}/MiniAudioStrip.img
            MiniAudioStrip
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/MiniAudioStrip.dsk ${WRAP_APM}
    COMMENT "Wrapping MiniAudioStrip.dsk in Apple Partition Map -> MiniAudioStrip.img"
    VERBATIM)
add_custom_target(MiniAudioStrip_disk ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/MiniAudioStrip.img)
