#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Compare the loadable content of PI32 ELF32 firmware files.

Builds of the same source by different toolchain builds (JieLi's Windows 2.5.3 and the Linux one in
tools/dx747/docker) or on different machines are never byte-identical as ELF files: the symbol and
string tables hold file names and the build paths. What matters for a flash image is the content of the
sections the chip loads (SHF_ALLOC), so this compares those, section by section and byte by byte, and the
function and data symbols by name, address and size.

  elf_report.py FILE.elf                 print a report: size and SHA-256 of every loadable section
  elf_report.py A.elf B.elf              compare two files and say what differs, where
  elf_report.py --symbols OUT FILE.elf   also write the sorted symbol list ("address size name") to OUT

Exit status of the two-file form: 0 when every loadable section is identical, 1 otherwise.
Standard library only (Python 3.8 or newer).
"""
import hashlib
import struct
import sys

SHF_ALLOC = 2
SHT_NOBITS = 8
SHT_SYMTAB = 2
STT_OBJECT, STT_FUNC = 1, 2


class Section:
    def __init__(self, name, kind, flags, addr, offset, size, link, entsize):
        self.name, self.kind, self.flags, self.addr = name, kind, flags, addr
        self.offset, self.size, self.link, self.entsize = offset, size, link, entsize


class Elf:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise SystemExit(f"{path}: not a 32-bit little-endian ELF file")
        (shoff,) = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
        raw = []
        for i in range(shnum):
            raw.append(struct.unpack_from("<10I", d, shoff + i * shentsize))
        names = raw[shstrndx]
        strings = d[names[4]:names[4] + names[5]]
        self.sections = []
        for name, kind, flags, addr, offset, size, link, _info, _align, entsize in raw:
            end = strings.index(b"\0", name)
            self.sections.append(Section(strings[name:end].decode("ascii", "replace"), kind, flags, addr, offset, size, link, entsize))

    def loadable(self):
        return [s for s in self.sections if s.flags & SHF_ALLOC and s.size]

    def content(self, s):
        return b"" if s.kind == SHT_NOBITS else self.data[s.offset:s.offset + s.size]

    def symbols(self):
        out = {}
        for s in self.sections:
            if s.kind != SHT_SYMTAB:
                continue
            table = self.sections[s.link]
            strings = self.data[table.offset:table.offset + table.size]
            for i in range(s.size // 16):
                name, value, size, info, _other, shndx = struct.unpack_from("<IIIBBH", self.data, s.offset + i * 16)
                if shndx == 0 or (info & 15) not in (STT_OBJECT, STT_FUNC):
                    continue
                end = strings.index(b"\0", name)
                out[strings[name:end].decode("ascii", "replace")] = (value, size)
        return out


def sha(data):
    return hashlib.sha256(data).hexdigest()


def loadable_digest(elf):
    h = hashlib.sha256()
    for s in sorted(elf.loadable(), key=lambda s: (s.addr, s.name)):
        h.update(f"{s.name}:{s.addr}:{s.size}:".encode())
        h.update(elf.content(s))
    return h.hexdigest()


def report(elf, symbols_out=None):
    print(f"file {elf.path}")
    print(f"elf_bytes {len(elf.data)} (informational: debug and string tables differ between builds)")
    for s in sorted(elf.loadable(), key=lambda s: (s.addr, s.name)):
        kind = "nobits" if s.kind == SHT_NOBITS else sha(elf.content(s))
        print(f"section {s.name} addr=0x{s.addr:08x} size={s.size} sha256={kind}")
    print(f"loadable_sha256 {loadable_digest(elf)}")
    syms = elf.symbols()
    lines = sorted(f"{v:08x} {z} {n}" for n, (v, z) in syms.items())
    print(f"symbols {len(lines)} sha256={sha(chr(10).join(lines).encode())}")
    if symbols_out:
        open(symbols_out, "w").write("\n".join(lines) + "\n")


def compare(a, b):
    same = True
    sa = {(s.name, s.addr): s for s in a.loadable()}
    sb = {(s.name, s.addr): s for s in b.loadable()}
    for key in sorted(set(sa) | set(sb), key=lambda k: (k[1], k[0])):
        name, addr = key
        if key not in sa or key not in sb:
            print(f"section {name} @0x{addr:08x}: only in {'A' if key in sa else 'B'}")
            same = False
            continue
        x, y = sa[key], sb[key]
        if x.size != y.size:
            print(f"section {name} @0x{addr:08x}: size differs, A={x.size} B={y.size}")
            same = False
            continue
        if x.kind == SHT_NOBITS or y.kind == SHT_NOBITS:
            print(f"section {name} @0x{addr:08x}: {x.size} bytes, no file content (zero-initialised)")
            continue
        ca, cb = a.content(x), b.content(y)
        if ca == cb:
            print(f"section {name} @0x{addr:08x}: identical, {x.size} bytes, sha256 {sha(ca)[:16]}...")
            continue
        diff = [i for i in range(len(ca)) if ca[i] != cb[i]]
        print(f"section {name} @0x{addr:08x}: DIFFERS in {len(diff)} of {len(ca)} bytes, first at offset 0x{diff[0]:x} (address 0x{addr + diff[0]:08x}), last at 0x{diff[-1]:x}")
        same = False
    ya, yb = a.symbols(), b.symbols()
    both = [n for n in ya if n in yb]
    moved = [n for n in both if ya[n] != yb[n]]
    print(f"symbols: {len(ya)} in A, {len(yb)} in B, {len(both)} in both, {len(moved)} with a different address or size, "
          f"{len([n for n in ya if n not in yb])} only in A, {len([n for n in yb if n not in ya])} only in B")
    for n in sorted(moved, key=lambda n: ya[n][0])[:10]:
        print(f"  {n}: A=0x{ya[n][0]:08x}/{ya[n][1]} B=0x{yb[n][0]:08x}/{yb[n][1]}")
    print("loadable content: " + ("IDENTICAL" if same else "DIFFERENT"))
    return same


def main(argv):
    symbols_out = None
    if len(argv) >= 3 and argv[0] == "--symbols":
        symbols_out, argv = argv[1], argv[2:]
    if len(argv) == 1:
        report(Elf(argv[0]), symbols_out)
        return 0
    if len(argv) == 2:
        return 0 if compare(Elf(argv[0]), Elf(argv[1])) else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
