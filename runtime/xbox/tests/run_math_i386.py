#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build/run a freestanding i386 test with synthetic vectors (no game files)."""
import argparse, subprocess, json, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--vectors',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--qemu',action='store_true');a=p.parse_args()
 a.output.mkdir(parents=True,exist_ok=True);v=a.vectors.resolve();out=a.output.resolve()
 if v.stat().st_size%40:raise ValueError('invalid 40-byte vector records')
 asm=out/'vectors.S';asm.write_text('.section .rodata\n.balign 8\n.global mgx_vectors_start,mgx_vectors_end\nmgx_vectors_start:\n.incbin '+json.dumps(str(v))+'\nmgx_vectors_end:\n.section .note.GNU-stack,"",@progbits\n')
 exe=out/'math-i386'
 cmd=['clang','-target','i386-linux-gnu','-march=pentium3','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-nostdlib','-static','-fno-pic','-fno-pie','-fuse-ld=lld','-Wl,-e,_start','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(ROOT/'include'),str(ROOT/'src/mgx_math.c'),str(ROOT/'tests/test_math_i386.c'),str(asm),'-o',str(exe)]
 subprocess.run(cmd,check=True)
 run=['qemu-i386','-cpu','pentium3',str(exe)] if a.qemu else [str(exe)]
 result=subprocess.run(run,check=True,text=True,capture_output=True,timeout=90);print(result.stdout,end='')
 report={'vector_count':v.stat().st_size//40,'target':'i386-linux-gnu/pentium3','execution':'qemu-i386 pentium3' if a.qemu else 'native i386 on build host','passed':True,'xbox_hardware_tested':False,'compile_command':cmd,'run_command':run}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
