#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile/run synthetic execution-bridge tests, without generated game data."""
import argparse,json,re,subprocess,tempfile,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler-source',type=Path,required=True);p.add_argument('--output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 src=a.compiler_source.resolve()/'src'
 with tempfile.TemporaryDirectory() as d:
  temp=Path(d);common=['clang','-std=c11','-O1','-g','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(src),'-I'+str(ROOT/'include')]
  if a.sanitize:common+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
  objects=[];commands=[];outputs=[];cases=checks=0
  def run(cmd):
   commands.append(cmd);subprocess.run(cmd,check=True,timeout=120)
  for f in [src/'cpu/cpu.c',ROOT/'src/mgx_dol.c',ROOT/'src/mgx_exec.c',ROOT/'src/mgx_math.c']:
   o=temp/(f.stem+'.o');cmd=common.copy()
   if f==src/'cpu/cpu.c':cmd+=['-include',str(ROOT/'include/mgx_math_redirect.h')]
   run(cmd+['-c',str(f),'-o',str(o)]);objects.append(str(o))
  for name in ['exec','boot_profile','pmu','hid4','code_writes','template','boot_irq','boot_audio','boot_exi','exi_probe']:
   obj=temp/(name+'.o');exe=temp/(name+'-tests')
   run(common+['-c',str(ROOT/'tests'/('test_'+name+'.c')),'-o',str(obj)])
   run(common+objects+[str(obj),'-lm','-o',str(exe)])
   result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   match=re.fullmatch(r'PASS: (\d+) .* cases; (\d+) checks\n',result.stdout)
   if not match:raise ValueError('missing test summary: '+name)
   cases+=int(match[1]);checks+=int(match[2]);outputs.append(result.stdout);print(result.stdout,end='')
  run([sys.executable,str(ROOT/'tests/test_template_tool.py'),'--compiler-source',str(a.compiler_source.resolve()),'-v'])
  if a.output:
   a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps({'passed':True,'cases':cases,'checks':checks,'sanitizers':a.sanitize,'output':''.join(outputs),'commands':commands,'game_data_used':False},indent=2)+'\n')
if __name__=='__main__':main()
