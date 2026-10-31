#!/usr/bin/env python3
"""
Resolve the PS5-style NID for a specific PLT entry in SceShellCore.elf, and
map every PLT address in the file to its NID.

Usage: find_plt_symbol.py <path to SceShellCore.elf> [target_plt_vaddr]

If a vaddr is given, prints just that entry's symbol. Otherwise prints the
whole PLT → NID map.
"""

import struct
import sys

VADDR_OFFSET = 0x4000


def main(path, target_plt=None):
    with open(path, "rb") as f:
        data = f.read()

    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 54)[0]
    e_phnum = struct.unpack_from("<H", data, 56)[0]

    PT_LOAD = 1
    PT_DYNAMIC = 2
    segments = []
    dynamic_seg = None

    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type = struct.unpack_from("<I", data, off)[0]
        p_offset = struct.unpack_from("<Q", data, off + 8)[0]
        p_vaddr = struct.unpack_from("<Q", data, off + 16)[0]
        p_filesz = struct.unpack_from("<Q", data, off + 32)[0]
        segments.append({'type': p_type, 'offset': p_offset,
                         'vaddr': p_vaddr, 'filesz': p_filesz})
        if p_type == PT_DYNAMIC:
            dynamic_seg = segments[-1]

    def vaddr_to_file(vaddr):
        for seg in segments:
            if seg['type'] == PT_LOAD:
                if seg['vaddr'] <= vaddr < seg['vaddr'] + seg['filesz']:
                    return vaddr - seg['vaddr'] + seg['offset']
        return vaddr + VADDR_OFFSET

    DT_NULL = 0
    DT_STRTAB = 5
    DT_SYMTAB = 6
    DT_JMPREL = 23
    DT_SCE_STRTAB = 0x61000035
    DT_SCE_SYMTAB = 0x61000039
    DT_SCE_JMPREL = 0x61000029
    DT_SCE_PLTRELSZ = 0x6100002b

    dyn_off = vaddr_to_file(dynamic_seg['vaddr'])
    dyn = {}
    i = 0
    while True:
        off = dyn_off + i * 16
        tag = struct.unpack_from("<q", data, off)[0]
        val = struct.unpack_from("<Q", data, off + 8)[0]
        if tag == DT_NULL:
            break
        dyn[tag] = val
        i += 1

    strtab_off = vaddr_to_file(dyn.get(DT_SCE_STRTAB, dyn.get(DT_STRTAB)))
    symtab_off = vaddr_to_file(dyn.get(DT_SCE_SYMTAB, dyn.get(DT_SYMTAB)))
    jmprel_off = vaddr_to_file(dyn.get(DT_SCE_JMPREL, dyn.get(DT_JMPREL)))

    def read_string(off):
        foff = strtab_off + off
        end = data.index(0, foff)
        return data[foff:end].decode("utf-8", errors="replace")

    plt_base = 0x186a000
    plt_end = 0x1872000

    results = {}
    for va in range(plt_base, plt_end, 16):
        foff = vaddr_to_file(va)
        if foff + 11 >= len(data):
            break
        if data[foff] != 0xFF or data[foff + 1] != 0x25:
            continue
        if data[foff + 6] != 0x68:
            continue
        idx = struct.unpack_from("<I", data, foff + 7)[0]
        roff = jmprel_off + idx * 24
        r_info = struct.unpack_from("<Q", data, roff + 8)[0]
        r_sym = r_info >> 32
        soff = symtab_off + r_sym * 24
        st_name = struct.unpack_from("<I", data, soff)[0]
        try:
            results[va] = read_string(st_name)
        except Exception:
            pass

    if target_plt is not None:
        name = results.get(target_plt)
        if name:
            print(f"PLT 0x{target_plt:08x}: {name}")
        else:
            print(f"PLT 0x{target_plt:08x}: not resolved")
    else:
        for va in sorted(results):
            print(f"0x{va:08x}: {results[va]}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <path to SceShellCore.elf> "
              f"[target_plt_vaddr]")
        sys.exit(1)
    target = int(sys.argv[2], 0) if len(sys.argv) >= 3 else None
    main(sys.argv[1], target)
