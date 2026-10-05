#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test real memory helpers in two i386 ABIs. Uses nxdk headers, not its kernel.

No Xbox execution is implied. The freestanding ELF is run under QEMU with a
Pentium III CPU when --qemu is specified. Calls cross separate object files.
"""
import argparse,json,subprocess,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--compiler-source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--qemu',action='store_true');p.add_argument('--compile-only',action='store_true');a=p.parse_args()
 if a.qemu and a.compile_only:p.error('choose execution or compile-only')
 sdk=a.sdk.resolve();src=a.compiler_source.resolve()/'src';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 stubs=out/'stdio-stop.c';stubs.write_text('#include <stdio.h>\nvoid *memcpy(void *d,const void *s,unsigned n){unsigned char *a=d;const unsigned char*b=s;for(unsigned i=0;i<n;++i)a[i]=b[i];return d;}\nFILE *stderr;\nint fprintf(FILE *f,const char *s,...){(void)f;(void)s;__asm__ volatile("int $0x80"::"a"(1),"b"(90));__builtin_unreachable();}\n')
 flags=['clang','-target','i386-linux-gnu','-march=pentium3','-ffreestanding','-fno-stack-protector','-fno-pie','-fno-pic','-ffunction-sections','-fdata-sections','-std=c11','-O1','-fshort-wchar','-fms-extensions','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(src),'-I'+str(sdk/'lib/pdclib/include'),'-I'+str(sdk/'lib/pdclib/platform/xbox/include'),'-I'+str(sdk/'lib/xboxrt/libc_extensions')]
 commands=[];reports=[]
 def run(cmd,**kw):
  commands.append(cmd)
  result=subprocess.run(cmd,capture_output=True,text=True,**kw)
  if result.returncode:raise RuntimeError(f'command failed ({result.returncode}): {cmd}\n{result.stdout}{result.stderr}')
  return result
 for abi in ['cdecl','fastcall']:
  options=flags+(['-DDOLRECOMP_X86_FASTCALL=1'] if abi=='fastcall' else [])
  objs=[]
  for f in [src/'cpu/cpu.c',ROOT/'tests/test_mem_abi_i386.c',stubs]:
   o=out/(abi+'-'+f.stem+'.o');cmd=options+['-c',str(f),'-o',str(o)];run(cmd,timeout=60);objs.append(str(o))
  exe=out/('memory-'+abi);cmd=['ld.lld','-m','elf_i386','--gc-sections','-e','_start',*objs,'-o',str(exe)];run(cmd,timeout=30)
  record={'abi':abi,'compiled':True,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'executed':False,'xbox_hardware_tested':False}
  if not a.compile_only:
   cmd=['qemu-i386','-cpu','pentium3',str(exe)] if a.qemu else [str(exe)]
   r=run(cmd,timeout=30)
   if not r.stdout.startswith('PASS:'):raise RuntimeError('missing ABI-test success')
   print(abi+': '+r.stdout,end='');record.update(executed=True,execution='qemu-pentium3' if a.qemu else 'host-i386',output=r.stdout)
  reports.append(record)
 # Independent PE32 link test: different conventions must not silently mix.
 caller=out/'caller.c';callee=out/'callee.c'
 caller.write_text('#include "cpu/cpu.h"\nvoid entry(void){volatile u32 n=mem_read32((CPUState*)0,7);(void)n;}\n')
 callee.write_text('#include "cpu/cpu.h"\nu32 mem_read32(CPUState*c,u32 a){(void)c;return a;}\n')
 peflags=['clang','-target','i386-pc-win32','-D__STDC__=1','-ffreestanding','-fno-stack-protector','-I'+str(src),'-I'+str(sdk/'lib/pdclib/include'),'-I'+str(sdk/'lib/pdclib/platform/xbox/include'),'-I'+str(sdk/'lib/xboxrt/libc_extensions')]
 for abi in ['cdecl','fastcall']:
  for source in [caller,callee]:
   run(peflags+(['-DDOLRECOMP_X86_FASTCALL=1'] if abi=='fastcall' else [])+['-c',str(source),'-o',str(out/(abi+'-'+source.stem+'.obj'))],timeout=30)
 for caller_abi in ['cdecl','fastcall']:
  for callee_abi in ['cdecl','fastcall']:
   exe=out/(caller_abi+'-'+callee_abi+'.exe')
   cmd=['lld','-flavor','link','/entry:entry','/subsystem:native','/machine:x86','/nodefaultlib','/out:'+str(exe),str(out/(caller_abi+'-caller.obj')),str(out/(callee_abi+'-callee.obj'))]
   commands.append(cmd);ret=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
   if caller_abi==callee_abi:
    if ret.returncode:raise RuntimeError(ret.stderr)
   elif not ret.returncode or 'mem_read32' not in ret.stderr:raise RuntimeError('mixed memory ABI was not rejected at PE link')
   (out/(caller_abi+'-'+callee_abi+'.log')).write_text(ret.stdout+ret.stderr)
 guard=out/'guard.c';guard.write_text('#include "cpu/cpu.h"\n')
 cmd=['clang','-fsyntax-only','-DDOLRECOMP_X86_FASTCALL=1','-I'+str(src),str(guard)]
 commands.append(cmd);ret=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
 if not ret.returncode or 'requires 32-bit x86' not in ret.stderr:raise RuntimeError('64-bit opt-in guard did not reject')
 (out/'guard.log').write_text(ret.stderr)
 print('PASS: both PE conventions link; both mixed links and x64 opt-in rejected')
 (out/'report.json').write_text(json.dumps({'tests':reports,'commands':commands,'game_data_used':False,'pe_matching_links':2,'pe_mixed_links_rejected':2,'x64_opt_in_rejected':True},indent=2)+'\n')
if __name__=='__main__':main()
