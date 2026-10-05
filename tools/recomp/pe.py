"""Minimal PE32 view of WET.EXE for the static recompiler.

Only what the recompiler needs: the memory image as the Windows loader would
map it at the preferred base, base relocations, the import address table and
the entry point. The Watcom linker writes VirtualSize 0 for most sections, so
the raw size (or the distance to the next section) is used instead.
"""
import hashlib
from dataclasses import dataclass, field

import pefile


@dataclass
class Import:
    slot: int          # guest VA of the IAT slot
    dll: str           # lower-case dll base name, e.g. "kernel32"
    name: str          # import name, e.g. "CreateFileA"

    @property
    def host_symbol(self):
        return f'h_{self.dll}_{self.name}'


@dataclass
class Section:
    name: str
    va: int
    size: int          # mapped size used by the recompiler
    raw: bytes
    executable: bool


@dataclass
class Image:
    path: str
    sha256: str
    base: int
    size: int
    entry: int
    stack_reserve: int
    sections: list = field(default_factory=list)
    relocs: set = field(default_factory=set)       # VAs of relocated dwords
    imports: dict = field(default_factory=dict)    # slot VA -> Import
    memory: bytearray = field(default_factory=bytearray)

    def text(self):
        return next(s for s in self.sections if s.executable)

    def in_text(self, va):
        t = self.text()
        return t.va <= va < t.va + t.size

    def read(self, va, n):
        off = va - self.base
        if off < 0 or off + n > len(self.memory):
            raise ValueError(f'read outside image: {va:#x}')
        return bytes(self.memory[off:off + n])

    def u32(self, va):
        return int.from_bytes(self.read(va, 4), 'little')


def load(path):
    data = open(path, 'rb').read()
    pe = pefile.PE(data=data, fast_load=False)
    opt = pe.OPTIONAL_HEADER
    img = Image(path=path, sha256=hashlib.sha256(data).hexdigest(), base=opt.ImageBase,
                size=opt.SizeOfImage, entry=opt.ImageBase + opt.AddressOfEntryPoint,
                stack_reserve=opt.SizeOfStackReserve)
    img.memory = bytearray(opt.SizeOfImage)
    img.memory[:opt.SizeOfHeaders] = data[:opt.SizeOfHeaders]
    secs = sorted(pe.sections, key=lambda s: s.VirtualAddress)
    for i, s in enumerate(secs):
        nxt = secs[i + 1].VirtualAddress if i + 1 < len(secs) else opt.SizeOfImage
        size = max(s.Misc_VirtualSize, s.SizeOfRawData)
        size = min(size, nxt - s.VirtualAddress) if s.Misc_VirtualSize == 0 else size
        raw = data[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData] if s.PointerToRawData else b''
        img.memory[s.VirtualAddress:s.VirtualAddress + len(raw)] = raw
        name = s.Name.rstrip(b'\0').decode('latin-1')
        img.sections.append(Section(name, opt.ImageBase + s.VirtualAddress, size, raw,
                                    bool(s.Characteristics & 0x20000000)))
    for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
        for e in block.entries:
            if e.type == 3:  # IMAGE_REL_BASED_HIGHLOW
                img.relocs.add(opt.ImageBase + e.rva)
    for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        dll = entry.dll.decode('latin-1').lower()
        if dll.endswith('.dll'):
            dll = dll[:-4]
        for imp in entry.imports:
            if imp.name is None:
                raise ValueError(f'ordinal import {dll}#{imp.ordinal} is not supported')
            img.imports[imp.address] = Import(imp.address, dll, imp.name.decode('latin-1'))
    return img
