#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile/run synthetic execution-bridge tests, without generated game data."""
import argparse,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler-source',type=Path,required=True);p.add_argument('--output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 src=a.compiler_source.resolve()/'src'
 with tempfile.TemporaryDirectory() as d:
  temp=Path(d);common=['clang','-std=c11','-O1','-g','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(src),'-I'+str(ROOT/'include')]
  if a.sanitize:common+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
  objects=[];commands=[]
  for f in [src/'cpu/cpu.c',ROOT/'src/mgx_dol.c',ROOT/'src/mgx_exec.c',ROOT/'src/mgx_math.c',ROOT/'tests/test_exec.c']:
   o=temp/(f.stem+'.o');cmd=common.copy()
   if f==src/'cpu/cpu.c':cmd+=['-include',str(ROOT/'include/mgx_math_redirect.h')]
   cmd+=['-c',str(f),'-o',str(o)];commands.append(cmd);subprocess.run(cmd,check=True);objects.append(str(o))
  exe=temp/'exec-tests';cmd=common+objects+['-lm','-o',str(exe)];commands.append(cmd);subprocess.run(cmd,check=True)
  result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=15);print(result.stdout,end='')
  boot_obj=temp/'boot.o';cmd=common+['-c',str(ROOT/'tests/test_boot_profile.c'),'-o',str(boot_obj)];commands.append(cmd);subprocess.run(cmd,check=True)
  boot_exe=temp/'boot-tests';cmd=common+objects[:-1]+[str(boot_obj),'-lm','-o',str(boot_exe)];commands.append(cmd);subprocess.run(cmd,check=True)
  boot=subprocess.run([str(boot_exe)],check=True,capture_output=True,text=True,timeout=15);print(boot.stdout,end='')
  if a.output:
   a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'passed':True,'cases':40,'sanitizers':a.sanitize,'output':result.stdout+boot.stdout,'commands':commands,'game_data_used':False},indent=2)+'\n')
if __name__=='__main__':main()
