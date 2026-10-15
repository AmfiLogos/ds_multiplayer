#!/usr/bin/env python3
"""Run single leaf functions of the Windows GameAssembly.dll (x64) on this Mac with unicorn (CPU emulation only;
no Windows, no Wine, no game launch). Used to compare the Windows CRT math (atan2f, acos, sinf, cosf ...)
bit for bit with Apple's libm.

    from win_emu import WinEmu
    e = WinEmu(); e.call_f32(0x4db470, 1.0, 2.0)

The CRT picks an FMA3 variant at run time when the CPU has FMA3 (global at FMA_FLAG). Unicorn (QEMU 5 TCG)
cannot run FMA3, so the emulator forces the SSE2 path (flag = 0). The FMA3 variants are compared statically.
"""
import os, struct
import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_PROT_ALL
from unicorn.x86_const import (UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_XMM0, UC_X86_REG_XMM1,
                               UC_X86_REG_XMM2, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_GS_BASE)

DLL = os.path.expanduser("~/ds_multiplayer/win/25846389/GameAssembly.dll")
FMA_FLAG = 0x520E4B8          # rva of the CRT "use FMA3" flag (atan2f: `and eax,3; cmp al,3`)
STOP = 0x10000000              # return address sentinel
STACK = 0x20000000
TEB = 0x30000000

class WinEmu:
    def __init__(self, dll=DLL, fma=0):
        pe = pefile.PE(dll, fast_load=True)
        self.base = base = pe.OPTIONAL_HEADER.ImageBase
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        uc = Uc(UC_ARCH_X86, UC_MODE_64)
        uc.mem_map(base, size, UC_PROT_ALL)
        uc.mem_write(base, pe.header)
        for s in pe.sections:
            data = s.get_data()
            uc.mem_write(base + s.VirtualAddress, data[: max(s.Misc_VirtualSize, 0) or len(data)])
        uc.mem_map(STOP, 0x1000, UC_PROT_ALL)
        uc.mem_write(STOP, b"\xC3")
        uc.mem_map(STACK, 0x100000, UC_PROT_ALL)
        uc.mem_map(TEB, 0x10000, UC_PROT_ALL)
        uc.reg_write(UC_X86_REG_GS_BASE, TEB)
        uc.mem_write(base + FMA_FLAG, struct.pack("<I", fma))
        self.uc = uc

    def _call(self, rva, xmms):
        uc = self.uc
        sp = STACK + 0x80000
        uc.mem_write(sp, struct.pack("<Q", STOP))
        uc.reg_write(UC_X86_REG_RSP, sp)
        for reg, raw in zip((UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2), xmms):
            uc.reg_write(reg, raw)
        uc.emu_start(self.base + rva, STOP, count=200000)
        return uc.reg_read(UC_X86_REG_XMM0)

    def call_f32(self, rva, *args):
        r = self._call(rva, [struct.unpack("<I", struct.pack("<f", a))[0] for a in args])
        return struct.unpack("<f", struct.pack("<I", r & 0xFFFFFFFF))[0]

    def call_f64(self, rva, *args):
        r = self._call(rva, [struct.unpack("<Q", struct.pack("<d", a))[0] for a in args])
        return struct.unpack("<d", struct.pack("<Q", r & 0xFFFFFFFFFFFFFFFF))[0]
