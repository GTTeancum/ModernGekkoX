#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('make_trace_bridge',Path(__file__).resolve().parents[1]/'tools/make_trace_bridge.py');tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
class TraceBridgeTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.p=Path(self.temp.name);self.g=self.p/'generated';(self.g/'chunks').mkdir(parents=True);self.t=self.p/'trace.json';self.o=self.p/'out'
 def tearDown(self):self.temp.cleanup()
 def chunk(self,name='one',symbol='func_80001000',address='80001000'):
  (self.g/'chunks'/f'{name}.c').write_text(f'void {symbol}(CPUState* ctx) {{\nswitch(ctx->pc){{case 0x{address}u: goto label_{address};}}\nlabel_{address}: return;}}\n')
 def trace(self,*pcs):self.t.write_text(json.dumps({'trace':list(pcs)}))
 def test_valid_fail_closed(self):
  self.chunk();self.trace('0x80001000','0x80001000');r=tool.generate(self.g,self.t,self.o)
  self.assertEqual(r['chunk_count'],1);self.assertEqual(r['entry_count'],1);self.assertFalse(r['complete_game']);self.assertIn('default:return 0',(self.o/'trace_bridge.c').read_text())
 def test_unknown_entry(self):
  self.chunk();self.trace('0x80001004')
  with self.assertRaises(ValueError):tool.generate(self.g,self.t,self.o)
 def test_ambiguous_entry(self):
  self.chunk();self.chunk('two','func_80002000');self.trace('0x80001000')
  with self.assertRaises(ValueError):tool.generate(self.g,self.t,self.o)
 def test_empty_trace(self):
  self.chunk();self.trace()
  with self.assertRaises(ValueError):tool.generate(self.g,self.t,self.o)
 def test_unsupported_source_shape(self):
  (self.g/'chunks'/'bad.c').write_text('/* not a generated chunk */');self.trace('0x80001000')
  with self.assertRaises(ValueError):tool.generate(self.g,self.t,self.o)
 def test_truncated_trace_rejected(self):
  self.chunk();self.t.write_text(json.dumps({'trace':['0x80001000'],'trace_truncated':True}))
  with self.assertRaises(ValueError):tool.generate(self.g,self.t,self.o)
 def test_reproducible(self):
  self.chunk();self.trace('0x80001000');a=tool.generate(self.g,self.t,self.o);b=tool.generate(self.g,self.t,self.o);self.assertEqual(a,b)
if __name__=='__main__':unittest.main()
