#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic template preparation, internal-label execution, and link gates."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_template',ROOT/'tools/prepare_template.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
COMPILER=None
SOURCE='''#include "../SYNTH.h"
void patched(CPUState *ctx,unsigned mode){
    if(mode==0)goto label_80002004;
    if(mode==1)goto label_80002000;
    ctx->gpr[9]=123;
label_80002000:
    ctx->pc = 0x80002000u;
    // 80002000: nop
label_80002004:
    ctx->pc = 0x80002004u;
    // 80002004: li      r3, 0
    ctx->gpr[3] = (u32)(s32)(0);
label_80002008:
    ctx->pc = 0x80002008u;
    return;
}
'''
MAIN=r'''
#include "mgx_exec.h"
#include "profile/template.h"
#include <stdlib.h>
#include <string.h>
extern void patched(CPUState*,unsigned);
static unsigned which;
static int dispatch(CPUState*c,uint32_t pc){
    (void)pc;c->pc=0x80001000;mem_write32(c,0x80002004,0x38600007);
    c->cr=0xabcdef01;patched(c,which);return 0;
}
int main(void){
 for(which=0;which<3;++which){
    CPUState*c=calloc(1,sizeof(*c));if(!c)return 1;
    c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);if(!c->ram)return 2;
    memcpy(c->ram+0x2000,mgx_template_original,12);
    const unsigned char writer[]={0x90,4,0,0};
    memcpy(c->ram+0x1000,writer,4);memcpy(c->ram+0x1008,writer,4);
    mgx_memory m={c->ram,c->ram_size,NULL,0};mgx_dol_plan p={0};
    p.count=1;p.entry=0x80001000;p.sections[0]=(mgx_dol_section){256,0x80001000,0x2000,0,0x1000,1};
    mgx_execution r;mgx_exec_run_template(&r,c,&m,&p,dispatch,2,MGX_BOOT_WII_CPU,MGX_TEMPLATE_PROFILE);
    if(strcmp(r.stop.reason,"untranslated-address")||c->gpr[3]!=7||c->cr!=0xabcdef01||r.template_instruction_reads!=1||r.template_writes!=1||c->pc!=0x80002008)return 3;
    if(c->gpr[9]!=(which==2?123u:0u))return 4;
    free(c->ram);free(c);
 }
 return 0;
}
'''

class TemplateToolTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.gen=self.root/'input';(self.gen/'chunks').mkdir(parents=True)
        (self.gen/'SYNTH.h').write_text('#include "mgx_exec.h"\n')
        (self.gen/'chunks/template.c').write_text(SOURCE)
        dol=bytearray(0x1200);struct.pack_into('>I',dol,0,256)
        struct.pack_into('>I',dol,0x48,0x80001000);struct.pack_into('>I',dol,0x90,0x1100)
        struct.pack_into('>I',dol,0xe0,0x80001000)
        struct.pack_into('>I',dol,0x100,0x90040000);struct.pack_into('>I',dol,0x108,0x90040000)
        original=bytes.fromhex('60000000386000004e800020');dol[0x1100:0x110c]=original
        self.dol=self.root/'synthetic.dol';self.dol.write_bytes(dol)
        self.settings={'dol_sha256':module.digest(dol),'template_sha256':module.digest(original),
          'address':0x80002000,'size':12,'patch_offset':4,'max_immediate':14,
          'writer_pc':[0x80001000,0x80001008],'source':'chunks/template.c','source_sha256':module.digest(SOURCE.encode())}
        self.sp=self.root/'spec.json';self.out=self.root/'output'
    def prepare(self):
        self.sp.write_text(json.dumps(self.settings));return module.prepare(self.gen,self.dol,self.sp,self.out)
    def rejected(self):
        with self.assertRaises((ValueError,TypeError)):self.prepare()
        self.assertFalse(self.out.exists())
    def test_full_copy_only_selected_instruction_changes(self):
        (self.gen/'untouched.txt').write_text('keep');r=self.prepare()
        self.assertEqual((self.gen/'chunks/template.c').read_text(),SOURCE)
        self.assertEqual((self.out/'untouched.txt').read_text(),'keep')
        self.assertEqual(r['original_files'],3)
        self.assertIn('mgx_exec_template_li(ctx, 0x80002004u)',(self.out/'chunks/template.c').read_text())
        self.assertIn(r['token'],(self.out/'profile/template.h').read_text())
    def test_dol_hash(self):self.settings['dol_sha256']='0'*64;self.rejected()
    def test_template_hash(self):self.settings['template_sha256']='0'*64;self.rejected()
    def test_source_hash(self):self.settings['source_sha256']='0'*64;self.rejected()
    def test_bad_offset(self):self.settings['patch_offset']=2;self.rejected()
    def test_bad_limit(self):self.settings['max_immediate']=32768;self.rejected()
    def test_bad_opcode(self):
        b=bytearray(self.dol.read_bytes());b[0x1104]=0x3c;self.dol.write_bytes(b)
        self.settings['dol_sha256']=module.digest(b);self.settings['template_sha256']=module.digest(b[0x1100:0x110c]);self.rejected()
    def test_non_store_writer(self):self.settings['writer_pc'][0]=0x80001004;self.rejected()
    def test_path_escape(self):self.settings['source']='../escape.c';self.rejected()
    def test_existing_output_preserved(self):
        self.out.mkdir();(self.out/'keep').write_text('keep')
        with self.assertRaises(ValueError):self.prepare()
        self.assertEqual((self.out/'keep').read_text(),'keep')
    def test_changed_generated_shape(self):
        b=SOURCE.replace('(u32)(s32)(0)','0u');(self.gen/'chunks/template.c').write_text(b)
        self.settings['source_sha256']=module.digest(b.encode());self.rejected()
    def test_duplicate_label(self):
        b=SOURCE+'\nlabel_80002004:\n';(self.gen/'chunks/template.c').write_text(b)
        self.settings['source_sha256']=module.digest(b.encode());self.rejected()
    def test_reproducible(self):
        self.prepare();first=(self.out/'chunks/template.c').read_bytes();self.out=self.root/'other';self.prepare()
        self.assertEqual(first,(self.out/'chunks/template.c').read_bytes())
    def test_internal_label_and_link_gate(self):
        if COMPILER is None:self.skipTest('--compiler-source required for integration')
        self.prepare();main=self.out/'main.c';main.write_text(MAIN)
        common=['clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
          '-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(COMPILER/'src'),'-I'+str(ROOT/'include'),'-I'+str(self.out)]
        exe=self.root/'test';objects=[]
        sources=[COMPILER/'src/cpu/cpu.c',ROOT/'src/mgx_exec.c',ROOT/'src/mgx_dol.c',ROOT/'src/mgx_math.c',main]
        for i,s in enumerate(sources):
            obj=self.root/f'{i}.o';cmd=common.copy()
            if s.name=='cpu.c':cmd+=['-include',str(ROOT/'include/mgx_math_redirect.h')]
            subprocess.run(cmd+['-c',str(s),'-o',str(obj)],check=True,capture_output=True,timeout=60);objects.append(str(obj))
        subprocess.run(common+objects+[str(self.out/'chunks/template.c'),'-lm','-o',str(exe)],check=True,capture_output=True,timeout=60)
        subprocess.run([str(exe)],check=True,capture_output=True,timeout=15)
        failed=subprocess.run(common+objects+[str(self.gen/'chunks/template.c'),'-lm','-o',str(self.root/'stale')],capture_output=True,text=True,timeout=60)
        self.assertNotEqual(failed.returncode,0);self.assertIn('mgx_template_instrumented_',failed.stderr)

if __name__=='__main__':
    p=argparse.ArgumentParser(add_help=False);p.add_argument('--compiler-source',type=Path);a,remaining=p.parse_known_args()
    COMPILER=a.compiler_source.resolve() if a.compiler_source else None
    unittest.main(argv=[__file__]+remaining)
