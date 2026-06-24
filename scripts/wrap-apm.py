#!/usr/bin/env python3
"""
wrap-apm.py -- wrap a raw HFS image in an Apple Partition Map so Mac OS 9
sees it as a valid mountable disk inside QEMU.

Usage:  wrap-apm.py <raw-hfs.dsk> <out.img> [volume-name]

Retro68's add_application() produces a "raw HFS" .dsk (HFS volume bytes
starting at offset 0, no driver descriptor record, no partition map).
Mac OS 9 expects a Driver Descriptor Record at sector 0 ("ER" signature)
and an Apple Partition Map starting at sector 1, with at least one entry
of type "Apple_HFS" pointing to the HFS payload.  This script builds that
structure and concatenates the original HFS payload after it.
"""

import struct
import sys
from pathlib import Path

BLOCK = 512

# pmPartStatus value that Apple Drive Setup writes for a mountable HFS data
# partition: 0x4400037F.  Decomposed:
#   bits 0-9 (0x037F): valid|allocated|inUse|bootInfo|readable|writable|
#                      positionIndependent|chainCompatible|realDriver|canChain
#   bit 22 (0x400000): partition is automatically mountable
#   bit 30 (0x40000000): mountable on startup
# Our wrapper produced 0x37 which OS 9 reads as "not eligible for automount."
DATA_PART_STATUS = 0x4400037F


def make_ddr(total_blocks: int) -> bytes:
    """Sector 0: Driver Descriptor Record (no boot drivers)."""
    ddr = bytearray(BLOCK)
    ddr[0:2] = b"ER"
    struct.pack_into(">H", ddr, 2, BLOCK)         # sbBlkSize
    struct.pack_into(">I", ddr, 4, total_blocks)  # sbBlkCount
    # sbDevType, sbDevId, sbData, sbDrvrCount all zero
    return bytes(ddr)


def make_apm_entry(map_blocks: int, p_start: int, p_size: int,
                   name: str, p_type: str, status: int) -> bytes:
    """One Apple Partition Map entry (sector-sized, 512 bytes)."""
    e = bytearray(BLOCK)
    e[0:2] = b"PM"                          # pmSig
    # bytes 2-3 reserved (zero)
    struct.pack_into(">I", e, 4,  map_blocks)   # pmMapBlkCnt
    struct.pack_into(">I", e, 8,  p_start)      # pmPyPartStart
    struct.pack_into(">I", e, 12, p_size)       # pmPartBlkCnt
    name_b = name.encode("ascii")[:31]
    e[16:16 + len(name_b)] = name_b
    type_b = p_type.encode("ascii")[:31]
    e[48:48 + len(type_b)] = type_b
    struct.pack_into(">I", e, 80, 0)            # pmLgDataStart
    struct.pack_into(">I", e, 84, p_size)       # pmDataCnt
    struct.pack_into(">I", e, 88, status)       # pmPartStatus
    # Boot info fields zero (no boot code in this partition)
    return bytes(e)


def wrap(in_path: Path, out_path: Path, volume_name: str = "Claude") -> None:
    hfs = bytearray(in_path.read_bytes())
    if len(hfs) < 1024:
        sys.exit(f"{in_path}: too small ({len(hfs)} bytes) to be a HFS volume")

    # Retro68 sets the volume's drAtrb lock bits (presumably so build output
    # isn't accidentally clobbered).  OS 9 won't mount a hardware-locked HFS
    # volume on a read-write disk, so clear drAtrb in both the primary MDB
    # (at sector 2 of the volume = byte offset 1024) and the alternate MDB
    # (1024 bytes before the end of the volume).
    if hfs[1024:1026] == b"BD":
        hfs[1024 + 10:1024 + 12] = b"\x00\x00"
    alt = len(hfs) - 1024
    if alt > 1024 and hfs[alt:alt + 2] == b"BD":
        hfs[alt + 10:alt + 12] = b"\x00\x00"

    # Pad payload to a sector boundary if needed.
    if len(hfs) % BLOCK:
        hfs += b"\x00" * (BLOCK - (len(hfs) % BLOCK))
    hfs_blocks = len(hfs) // BLOCK

    # Apple convention reserves 63 blocks for the APM region (legacy SCSI
    # sizing).  Two of those 63 are valid entries (APM-self + HFS); the rest
    # are zero-filled.  Layout:
    #   sector 0           DDR
    #   sector 1           APM entry: Apple_partition_map ("Apple")
    #   sector 2           APM entry: Apple_HFS (the data partition)
    #   sectors 3..63      Zero padding (reserved for additional APM entries)
    #   sectors 64..       HFS payload
    APM_REGION_BLOCKS = 63
    VALID_APM_ENTRIES = 2

    hfs_start = 1 + APM_REGION_BLOCKS  # = 64
    total_blocks = hfs_start + hfs_blocks

    ddr = make_ddr(total_blocks)

    # APM-self uses status=0 (matches what Apple Drive Setup writes).
    apm_self = make_apm_entry(
        map_blocks=VALID_APM_ENTRIES,
        p_start=1,
        p_size=APM_REGION_BLOCKS,
        name="Apple",
        p_type="Apple_partition_map",
        status=0,
    )
    apm_hfs = make_apm_entry(
        map_blocks=VALID_APM_ENTRIES,
        p_start=hfs_start,
        p_size=hfs_blocks,
        name=volume_name,
        p_type="Apple_HFS",
        status=DATA_PART_STATUS,
    )

    # Zero-padded APM region trailing entries
    apm_padding = b"\x00" * (BLOCK * (APM_REGION_BLOCKS - VALID_APM_ENTRIES))

    with out_path.open("wb") as f:
        f.write(ddr)
        f.write(apm_self)
        f.write(apm_hfs)
        f.write(apm_padding)
        f.write(hfs)

    print(f"{out_path}: DDR + APM(63 reserved, 2 entries) + HFS({hfs_blocks} blocks) "
          f"= {total_blocks} blocks total ({total_blocks * BLOCK} bytes)")


if __name__ == "__main__":
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        sys.exit(__doc__.strip())
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    name = sys.argv[3] if len(sys.argv) == 4 else "Claude"
    wrap(src, dst, name)
