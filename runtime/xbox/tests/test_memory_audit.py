#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,struct,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('audit',Path(__file__).resolve().parents[1]/'tools/audit_pe_memory.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def fixture():
 b=bytearray(1024);b[:2]=b'MZ';struct.pack_into('<I',b,60,128);b[128:132]=b'PE\0\0'
 struct.pack_into('<HH',b,132,0x14c,1);struct.pack_into('<H',b,148,224)
 h=152;struct.pack_into('<H',b,h,0x10b)
 for at,v in [(h+32,4096),(h+56,8192),(h+60,512)]:struct.pack_into('<I',b,at,v)
 a=h+224;b[a:a+8]=b'.text\0\0\0';struct.pack_into('<4I',b,a+8,256,4096,512,512);struct.pack_into('<I',b,a+36,0x60000020)
 return b
class MemoryAudit(unittest.TestCase):
 def run_audit(self,b,mem1=24*1024*1024,physical=64*1024*1024):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'main.exe';p.write_bytes(b);return m.audit(p,mem1,physical)
 def test_valid(self):
  r=self.run_audit(fixture());self.assertEqual(r['pe_image_bytes'],8192);self.assertEqual(r['sections'][0]['mapped_bytes'],4096);self.assertTrue(r['sections'][0]['executable']);self.assertIsNone(r['fits_hardware']);self.assertFalse(r['hardware_measured'])
 def test_budget(self):self.assertEqual(self.run_audit(fixture(),mem1=8192,physical=12288)['minimum_over_budget_bytes'],4096)
 def test_invalid_budget(self):
  for mem,phys in [(-1,64),(1,0)]:
   with self.assertRaises(ValueError):self.run_audit(fixture(),mem,phys)
 def test_truncation(self):
  for n in [0,60,128,140,260,400,600]:
   with self.subTest(n=n),self.assertRaises(ValueError):self.run_audit(fixture()[:n])
 def test_machine(self):
  b=fixture();struct.pack_into('<H',b,132,0x8664)
  with self.assertRaises(ValueError):self.run_audit(b)
 def test_optional_header(self):
  b=fixture();struct.pack_into('<H',b,152,0x20b)
  with self.assertRaises(ValueError):self.run_audit(b)
 def test_section_bounds(self):
  for at,v in [(384,0x3000),(388,0x1001),(392,0x3000),(396,2048)]:
   b=fixture();struct.pack_into('<I',b,at,v)
   with self.subTest(at=at),self.assertRaises(ValueError):self.run_audit(b)
 def test_alignment(self):
  b=fixture();struct.pack_into('<I',b,184,1234)
  with self.assertRaises(ValueError):self.run_audit(b)
 def test_overlap(self):
  b=fixture();struct.pack_into('<H',b,134,2);b[416:456]=b[376:416]
  with self.assertRaises(ValueError):self.run_audit(b)
if __name__=='__main__':unittest.main()
