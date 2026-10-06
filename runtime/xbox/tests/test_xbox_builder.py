#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cache and optional real-nxdk link tests using generated synthetic code only."""
import argparse,copy,importlib.util,json,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import build_xbox_probe as build
p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path);p.add_argument('--compiler-source',type=Path)
a,rest=p.parse_known_args()
class CacheTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
  self.s=self.p/'s.c';self.s.write_text('int x;');self.h=self.p/'s.h';self.h.write_text('/* header */');self.o=self.p/'s.obj';self.o.write_bytes(b'synthetic-object')
  self.ident={'version':'synthetic'};self.item={'source':str(self.s),'object':str(self.o),'object_sha256':build.sha(self.o),'dependencies':{str(self.s):build.sha(self.s),str(self.h):build.sha(self.h)}}
 def tearDown(self):self.tmp.cleanup()
 def valid(self,identity=None):return build.reusable(self.item,self.s,self.ident,self.ident if identity is None else identity)
 def test_valid(self):self.assertTrue(self.valid())
 def test_old_identity_rejected(self):self.assertFalse(build.reusable(self.item,self.s,self.ident,None))
 def test_wrong_identity(self):self.assertFalse(self.valid({'version':'other'}))
 def test_header_change(self):self.h.write_text('changed');self.assertFalse(self.valid())
 def test_source_change(self):self.s.write_text('changed');self.assertFalse(self.valid())
 def test_object_change(self):self.o.write_bytes(b'changed');self.assertFalse(self.valid())
 def test_missing_dependency(self):self.h.unlink();self.assertFalse(self.valid())
 def test_empty_dependencies(self):self.item['dependencies']={};self.assertFalse(self.valid())
 def test_startup_profile_flags(self):
  self.assertEqual(build.startup_profile_flags('serial'),[])
  self.assertEqual(build.startup_profile_flags('si-poll-dormant'),['-DMGX_STARTUP_PROFILE=MGX_BOOT_WII_SI_POLL_DORMANT'])
  self.assertEqual(build.startup_profile_flags('vi-clock-ntsc'),['-DMGX_STARTUP_PROFILE=MGX_BOOT_WII_VI_CLOCK_NTSC'])
  self.assertEqual(build.startup_profile_flags('vi-clock-27mhz'),['-DMGX_STARTUP_PROFILE=MGX_BOOT_WII_VI_CLOCK_27MHZ'])
 def test_pe_validation(self):
  pe=self.p/'test.exe';pe.write_bytes(b'bad')
  with self.assertRaises(ValueError):build.pe_image_size(pe)
  data=bytearray(256);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x14c);struct.pack_into('<I',data,64+24+56,4096);pe.write_bytes(data)
  self.assertEqual(build.pe_image_size(pe),4096);struct.pack_into('<H',data,68,0x8664);pe.write_bytes(data)
  with self.assertRaises(ValueError):build.pe_image_size(pe)
@unittest.skipUnless(a.sdk and a.compiler_source,'supply --sdk and --compiler-source for actual nxdk linking')
class NxdkLink(unittest.TestCase):
 def test_full_build_reuse_and_flag_rejection(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);g=p/'generated';(g/'chunks').mkdir(parents=True)
   (g/'synthetic.h').write_text('#include "cpu/cpu.h"\nvoid synthetic_chunk(CPUState*);\nstatic inline int dolrecomp_call(CPUState*c,u32 a){if(a!=0x80001000)return 0;synthetic_chunk(c);return 1;}\n')
   (g/'synthetic.c').write_text('#include "synthetic.h"\n')
   (g/'chunks/one.c').write_text('#include "../synthetic.h"\nvoid synthetic_chunk(CPUState*c){c->gpr[3]=mem_read32(c,0x80001000);c->pc=0;}\n')
   base=[sys.executable,str(ROOT/'tools/build_xbox_probe.py'),'--sdk',str(a.sdk.resolve()),'--compiler-source',str(a.compiler_source.resolve()),'--generated',str(g),'--optimization','z']
   result=subprocess.run(base+['--output',str(p/'first')],capture_output=True,text=True,timeout=120);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   first=json.loads((p/'first/build-report.json').read_text());self.assertEqual(first['generated_chunks'],1);self.assertEqual(first['compiled_objects'],8)
   subprocess.run(base+['--output',str(p/'second'),'--reuse-report',str(p/'first/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   second=json.loads((p/'second/build-report.json').read_text());self.assertEqual(second['reused_verified_objects'],8);self.assertEqual(second['compiled_objects'],0)
   self.assertEqual((p/'first/main.exe').read_bytes(),(p/'second/main.exe').read_bytes())
   self.assertFalse(first['game_booted']);self.assertFalse(first['xbox_hardware_tested'])
   # Opt-in compiles every caller and helper together. A cdecl report must not
   # reuse even one object into the different ABI despite unchanged source.
   subprocess.run(base+['--memory-abi','fastcall','--output',str(p/'fast'),'--reuse-report',str(p/'first/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   fast=json.loads((p/'fast/build-report.json').read_text());self.assertEqual(fast['compiled_objects'],8);self.assertEqual(fast['reused_verified_objects'],0);self.assertEqual(fast['memory_abi'],'fastcall')
   subprocess.run(base+['--memory-abi','fastcall','--output',str(p/'fast-repeat'),'--reuse-report',str(p/'fast/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   repeated=json.loads((p/'fast-repeat/build-report.json').read_text());self.assertEqual(repeated['reused_verified_objects'],8)
   self.assertEqual((p/'fast/main.exe').read_bytes(),(p/'fast-repeat/main.exe').read_bytes())
   self.assertFalse(first['machine_outliner'])
   outlined_args=base+['--memory-abi','fastcall','--machine-outliner']
   subprocess.run(outlined_args+['--output',str(p/'outlined'),'--reuse-report',str(p/'fast/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   outlined=json.loads((p/'outlined/build-report.json').read_text())
   self.assertTrue(outlined['machine_outliner']);self.assertEqual(outlined['compiled_objects'],8);self.assertEqual(outlined['reused_verified_objects'],0)
   self.assertTrue(all('-enable-machine-outliner=always' in r['compile_key'] for r in outlined['objects']))
   subprocess.run(outlined_args+['--output',str(p/'outlined-repeat'),'--reuse-report',str(p/'outlined/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   outline_repeat=json.loads((p/'outlined-repeat/build-report.json').read_text());self.assertEqual(outline_repeat['reused_verified_objects'],8)
   self.assertEqual((p/'outlined/main.exe').read_bytes(),(p/'outlined-repeat/main.exe').read_bytes())
   subprocess.run(base+['--memory-abi','fastcall','--output',str(p/'back-to-default'),'--reuse-report',str(p/'outlined/build-report.json')],check=True,capture_output=True,text=True,timeout=120)
   back=json.loads((p/'back-to-default/build-report.json').read_text());self.assertFalse(back['machine_outliner']);self.assertEqual(back['reused_verified_objects'],0)
   self.assertEqual((p/'fast/main.exe').read_bytes(),(p/'back-to-default/main.exe').read_bytes())
   self.assertEqual(outlined['boot_profile'],'serial')
   previous=p/'outlined/build-report.json'
   for profile,constant in [('vi-clock-ntsc','MGX_BOOT_WII_VI_CLOCK_NTSC'),('vi-clock-27mhz','MGX_BOOT_WII_VI_CLOCK_27MHZ'),('si-poll-dormant','MGX_BOOT_WII_SI_POLL_DORMANT'),('serial',None)]:
    dest=p/('profile-'+profile)
    subprocess.run(outlined_args+['--boot-profile',profile,'--output',str(dest),'--reuse-report',str(previous)],check=True,capture_output=True,text=True,timeout=120)
    result=json.loads((dest/'build-report.json').read_text())
    self.assertEqual(result['boot_profile'],profile);self.assertEqual(result['compiled_objects'],1);self.assertEqual(result['reused_verified_objects'],7)
    for record in result['objects']:
     selected=[f for f in record['compile_key'] if f.startswith('-DMGX_STARTUP_PROFILE=')]
     expected=['-DMGX_STARTUP_PROFILE='+constant] if constant and record['source'].endswith('/startup-probe/main.c') else []
     self.assertEqual(selected,expected)
    previous=dest/'build-report.json'
   self.assertEqual((p/'outlined/main.exe').read_bytes(),(p/'profile-serial/main.exe').read_bytes())
   wrong=base.copy();wrong[-1]='1'
   result=subprocess.run(wrong+['--output',str(p/'wrong'),'--reuse-report',str(p/'first/build-report.json')],capture_output=True,text=True,timeout=15)
   self.assertNotEqual(result.returncode,0);self.assertFalse((p/'wrong/default.xbe').exists())
if __name__=='__main__':unittest.main(argv=['test_xbox_builder.py',*rest])
