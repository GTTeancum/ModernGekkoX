#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host regression tests. Only synthetic byte fixtures; no game data required."""
import ctypes as C
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class Memory(C.Structure):
    _fields_ = [('mem1', C.c_void_p), ('mem1_size', C.c_uint32), ('mem2', C.c_void_p), ('mem2_size', C.c_uint32)]
class Section(C.Structure):
    _fields_ = [(x, C.c_uint32) for x in ['file_offset','address','size','bank','bank_offset','executable']]
class Plan(C.Structure):
    _fields_ = [('sections',Section*18)]+[(x,C.c_uint32) for x in ['count','entry','bss_address','bss_size']]
Reader = C.CFUNCTYPE(C.c_bool, C.c_void_p, C.c_uint64, C.c_void_p, C.c_uint32)
TMP = tempfile.TemporaryDirectory(prefix='mgx-dol-tests-')
LIB = Path(TMP.name)/'loader.so'
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fPIC','-shared','-O2',
                '-I'+str(ROOT/'include'),str(ROOT/'src/mgx_dol.c'),'-o',str(LIB)],check=True)
DLL = C.CDLL(str(LIB))
DLL.mgx_dol_load.argtypes = [Reader,C.c_void_p,C.c_uint64,C.POINTER(Memory),C.POINTER(Plan)]
DLL.mgx_dol_load.restype = C.c_int
DLL.mgx_memory_pointer.argtypes = [C.POINTER(Memory),C.c_uint32,C.c_uint32]
DLL.mgx_memory_pointer.restype = C.c_void_p

def put(b,offset,value): struct.pack_into('>I',b,offset,value)
def fixture():
    b=bytearray(288)
    for i,off,addr,size in [(0,256,0x80000020,16),(7,272,0x80000050,16)]:
        put(b,i*4,off); put(b,0x48+i*4,addr); put(b,0x90+i*4,size)
    put(b,0xd8,0x80000040); put(b,0xdc,32); put(b,0xe0,0x80000020)
    b[256:]=bytes(range(32))
    return b

def load(b,mem2=0,fail_at=None):
    ram=(C.c_uint8*4096)(*([0xa5]*4096)); extra=(C.c_uint8*mem2)() if mem2 else None
    m=Memory(C.addressof(ram),4096,C.addressof(extra) if extra is not None else None,mem2)
    p=Plan(); p.entry=0xdeadbeef; reads=[]
    def reader(_,off,dest,n):
        reads.append(off)
        if off==fail_at or off+n>len(b): return False
        C.memmove(dest,bytes(b[off:off+n]),n); return True
    callback=Reader(reader)
    rc=DLL.mgx_dol_load(callback,None,len(b),C.byref(m),C.byref(p))
    return rc,bytes(ram),p,reads

class LoaderTests(unittest.TestCase):
    def test_load_and_entry(self):
        rc,ram,p,_=load(fixture()); self.assertEqual(rc,0); self.assertEqual(p.count,2)
        self.assertEqual(p.entry,0x80000020); self.assertEqual(ram[32:48],bytes(range(16)))
    def test_bss_envelope_preserves_initialized_data(self):
        rc,ram,_,_=load(fixture()); self.assertEqual(rc,0)
        self.assertEqual(ram[64:80],bytes(16)); self.assertEqual(ram[80:96],bytes(range(16,32)))
        self.assertEqual(ram[96],0xa5)
    def test_truncated_header(self): self.assertEqual(load(fixture()[:255])[0],2)
    def test_truncated_section(self): self.assertEqual(load(fixture()[:-1])[0],2)
    def test_header_as_section_rejected(self):
        b=fixture(); put(b,0,240); self.assertEqual(load(b)[0],2)
    def test_file_offset_overflow_rejected(self):
        b=fixture(); put(b,0,0xfffffff0); self.assertEqual(load(b)[0],2)
    def test_unbacked_range_before_writes(self):
        b=fixture(); put(b,0x48,0x817ffff8); rc,ram,p,reads=load(b)
        self.assertEqual(rc,3); self.assertEqual(ram,bytes([0xa5])*4096)
        self.assertEqual(p.entry,0); self.assertEqual(reads,[0])
    def test_unbacked_bss(self):
        b=fixture(); put(b,0xdc,0xffffffff); self.assertEqual(load(b)[0],3)
    def test_wrapping_address(self):
        b=fixture(); put(b,0x48,0xfffffff8); self.assertEqual(load(b)[0],3)
    def test_uncached_alias(self):
        b=fixture(); put(b,0x48,0xc0000020); self.assertEqual(load(b)[0],0)
    def test_physical_alias(self):
        b=fixture(); put(b,0x48,0x20); self.assertEqual(load(b)[0],0)
    def test_alias_section_overlap(self):
        b=fixture(); put(b,0x48+7*4,0xc0000028); self.assertEqual(load(b)[0],4)
    def test_unaligned_entry(self):
        b=fixture(); put(b,0xe0,0x80000021); self.assertEqual(load(b)[0],5)
    def test_data_entry_rejected(self):
        b=fixture(); put(b,0xe0,0x80000050); self.assertEqual(load(b)[0],5)
    def test_empty_image_rejected(self):
        self.assertEqual(load(bytearray(256))[0],5)
    def test_missing_mem2(self):
        b=fixture(); put(b,0x48,0x90000020); put(b,0xe0,0x90000020)
        self.assertEqual(load(b)[0],3); self.assertEqual(load(b,mem2=4096)[0],0)
    def test_io_failure_does_not_publish_entry(self):
        rc,_,p,_=load(fixture(),fail_at=272); self.assertEqual(rc,6); self.assertEqual(p.entry,0)
    def test_mmio_is_not_ram(self):
        b=fixture(); put(b,0x48,0xcc000000); self.assertEqual(load(b)[0],3)

if __name__=='__main__': unittest.main()
