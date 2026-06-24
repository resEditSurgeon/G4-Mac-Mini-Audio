cmake_minimum_required(VERSION 3.10)
project(MiniAudioTool C)

# Tier 1 interactive amp/mute control + GPIO probe.
# Build with the Retro68 PowerPC toolchain:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=$RETRO68/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   make

add_application(MiniAudioTool
    MiniAudioTool.c
    MiniAudioTool.r
    TYPE "APPL"
    CREATOR "MmAt"
)

# Same CFM import libraries the v8 INIT links against.
target_link_libraries(MiniAudioTool
    NameRegistryLib
    DriverServicesLib
    SoundLib          # SoundComponentGetInfo (Tier 3 component probe)
)

set_target_properties(MiniAudioTool PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-Wno-multichar")
set_target_properties(MiniAudioTool PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
