#!/usr/bin/env python3
"""
Find how SceShellCore checks dipsw flags around DDD code paths.

Usage: find_dipsw_usage.py <path to SceShellCore.elf>

Walks the binary, locates every reference to a known dipsw-related string
(for example "/dev/dipsw" or "SCE_KERNEL_DIPSW_ENABLE_DDD_EMU"), and prints
the surrounding instructions so the context around each check is visible.
"""

import struct
import sys

VADDR_OFFSET = 0x4000
CODE_FILE_START = 0x4000


def main(path):
    with open(path, "rb") as f:
        data = f.read()

    code_end = min(len(data), 0x1877000)

    print("=== dipsw-related strings ===")
    dipsw_strings = {}
    for needle in [b"dipsw", b"Dipsw", b"DIPSW",
                   b"/dev/dipsw", b"SCE_KERNEL_DIPSW"]:
        pos = 0
        while True:
            pos = data.find(needle, pos)
            if pos < 0:
                break
            str_start = pos
            while str_start > 0 and data[str_start - 1] != 0:
                str_start -= 1
            str_end = pos
            while str_end < len(data) and data[str_end] != 0:
                str_end += 1
            s = data[str_start:str_end].decode("utf-8", errors="replace")
            vaddr = str_start - VADDR_OFFSET
            if vaddr not in dipsw_strings:
                dipsw_strings[vaddr] = s
                print(f"  vaddr 0x{vaddr:08x}: \"{s[:100]}\"")
            pos = str_end

    modrm_base = {0x05: "rax", 0x0d: "rcx", 0x15: "rdx", 0x1d: "rbx",
                  0x25: "rsp", 0x2d: "rbp", 0x35: "rsi", 0x3d: "rdi"}
    modrm_ext  = {0x05: "r8",  0x0d: "r9",  0x15: "r10", 0x1d: "r11",
                  0x25: "r12", 0x2d: "r13", 0x35: "r14", 0x3d: "r15"}

    print("\n=== LEA refs to dipsw strings ===")
    for file_off in range(CODE_FILE_START, code_end - 7):
        b0 = data[file_off]
        if b0 != 0x48 and b0 != 0x4C:
            continue
        if data[file_off + 1] != 0x8d:
            continue
        modrm = data[file_off + 2]
        if modrm not in modrm_base:
            continue
        disp32 = struct.unpack_from("<i", data, file_off + 3)[0]
        insn_va = file_off - VADDR_OFFSET
        target_va = insn_va + 7 + disp32

        if target_va in dipsw_strings:
            reg = modrm_base[modrm] if b0 == 0x48 else modrm_ext[modrm]
            s = dipsw_strings[target_va]
            print(f"\n  \"{s[:60]}...\" referenced at:")
            print(f"    vaddr 0x{insn_va:08x} via LEA {reg}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path to SceShellCore.elf>")
        sys.exit(1)
    main(sys.argv[1])
