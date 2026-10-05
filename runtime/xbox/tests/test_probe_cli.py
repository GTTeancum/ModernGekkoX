#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test host snapshot CLI using a synthetic DOL and synthetic dispatch only."""
import argparse,json,struct,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--compiler-source',type=Path,required=True)
a,rest=p.parse_known_args()
class ProbeCLI(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name);src=a.compiler_source.resolve()/'src'
  stub=cls.root/'stub.c';stub.write_text('#include "mgx_exec.h"\nint mgx_generated_dispatch(CPUState*c,uint32_t a){(void)a;mem_write32(c,0x80002000,0x12345678);return 0;}\n')
  cls.exe=cls.root/'probe';files=[ROOT/'startup-probe/main.c',ROOT/'src/mgx_exec.c',ROOT/'src/mgx_dol.c',ROOT/'src/mgx_math.c',src/'cpu/cpu.c',stub]
  cmd=['clang','-std=c11','-O1','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(src),'-I'+str(ROOT/'include'),*map(str,files),'-lm','-o',str(cls.exe)]
  subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=60)
  cls.dol=cls.root/'synthetic.dol';h=bytearray(256)
  for off,v in [(0,256),(0x48,0x80001000),(0x90,4),(0xe0,0x80001000)]:struct.pack_into('>I',h,off,v)
  cls.dol.write_bytes(h+bytes.fromhex('60000000'))
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def invoke(self,*args):return subprocess.run([str(self.exe),str(self.dol),*map(str,args)],capture_output=True,text=True,timeout=10)
 def test_snapshot_bytes(self):
  f=self.root/'mem1.bin';r=self.invoke('--dump-mem1',f);self.assertEqual(r.returncode,0,r.stderr)
  b=f.read_bytes();self.assertEqual(len(b),24*1024*1024);self.assertEqual(b[0x1000:0x1004],bytes.fromhex('60000000'));self.assertEqual(b[0x2000:0x2004],bytes.fromhex('12345678'))
  self.assertFalse(json.loads(r.stdout)['game_booted'])
 def test_strict_and_snapshot(self):
  f=self.root/'strict.bin';r=self.invoke('--strict','--dump-mem1',f);self.assertEqual(r.returncode,0,r.stderr)
  self.assertEqual(json.loads(r.stdout)['bootstrap'],'zero-state-plus-DOL');self.assertTrue(f.is_file())
 def test_audio_profile_default(self):
  r=self.invoke('--audio-only');self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout)
  self.assertEqual(d['bootstrap'],'reference-wii-audio-idle');self.assertEqual(d['dsp_control'],'0x00000804');self.assertEqual(d['ai_control'],'0x00000042');self.assertEqual(d['pi_cause'],'0x00010100');self.assertEqual(d['mmio_events'],[])
 def test_exi_profile_default(self):
  r=self.invoke();self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout)
  self.assertEqual(d['bootstrap'],'reference-wii-exi-absent-sp1')
  self.assertEqual(d['exi_status'],['0x00000800','0x00000880','0x00000000'])
  self.assertEqual(d['exi_reads'],[0,0,0]);self.assertEqual(d['exi_writes'],[0,0,0])
 def test_exi_legacy(self):
  r=self.invoke('--exi-only');self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout)
  self.assertEqual(d['bootstrap'],'reference-wii-exi-no-cards');self.assertEqual(d['exi_probe_transfers'],0)
 def test_exi_conflicts(self):
  for other in ['--strict','--cpu-only','--irq-only','--audio-only','--exi-only']:
   for args in [('--exi-only',other),(other,'--exi-only')]:self.assertEqual(self.invoke(*args).returncode,2)
 def test_audio_conflicts(self):
  for other in ['--strict','--cpu-only','--irq-only','--audio-only']:
   for args in [('--audio-only',other),(other,'--audio-only')]:self.assertEqual(self.invoke(*args).returncode,2)
 def test_irq_only_profile(self):
  r=self.invoke('--irq-only');self.assertEqual(r.returncode,0,r.stderr);d=json.loads(r.stdout)
  self.assertEqual(d['bootstrap'],'reference-wii-irq-init');self.assertEqual(d['dsp_control'],'0x00000000')
 def test_irq_conflicts(self):
  for other in ['--strict','--cpu-only','--irq-only']:
   for args in [('--irq-only',other),(other,'--irq-only')]:self.assertEqual(self.invoke(*args).returncode,2)
 def test_cpu_only_profile(self):
  r=self.invoke('--cpu-only');self.assertEqual(r.returncode,0,r.stderr)
  self.assertEqual(json.loads(r.stdout)['bootstrap'],'dolphin-wii-cpu-only')
 def test_conflicting_profiles(self):
  self.assertEqual(self.invoke('--strict','--cpu-only').returncode,2)
  self.assertEqual(self.invoke('--cpu-only','--strict').returncode,2)
 def test_duplicate_cpu_only(self):self.assertEqual(self.invoke('--cpu-only','--cpu-only').returncode,2)
 def test_missing_argument(self):self.assertEqual(self.invoke('--dump-mem1').returncode,2)
 def test_duplicate_flag(self):self.assertEqual(self.invoke('--strict','--strict').returncode,2)
 def test_existing_file_unchanged(self):
  f=self.root/'existing.bin';f.write_bytes(b'preserve');self.assertEqual(self.invoke('--dump-mem1',f).returncode,7);self.assertEqual(f.read_bytes(),b'preserve')
 def test_input_alias_unchanged(self):
  f=self.root/'alias.dol';f.symlink_to(self.dol);before=self.dol.read_bytes()
  self.assertEqual(self.invoke('--dump-mem1',f).returncode,7);self.assertEqual(self.dol.read_bytes(),before)
 def test_input_same_path(self):
  before=self.dol.read_bytes();self.assertEqual(self.invoke('--dump-mem1',self.dol).returncode,2);self.assertEqual(self.dol.read_bytes(),before)
if __name__=='__main__':unittest.main(argv=['test_probe_cli.py',*rest])
