#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make a trace-restricted, fail-closed diagnostic bridge.
This is NOT whole-game dead-code elimination. Only dispatcher entries observed
in the supplied trace are permitted. Unknown entries return failure rather than
running an emulator fallback or skipping a call. Generated output stays private.
"""
import argparse,hashlib,json,re
from pathlib import Path
def generate(generated,trace,output):
 generated=Path(generated);trace=Path(trace);output=Path(output)
 report=json.loads(trace.read_text())
 if report.get('trace_truncated'):raise ValueError('truncated traces cannot define a diagnostic closure')
 addresses=sorted(set(int(x,0) for x in report['trace']))
 if not addresses:raise ValueError('empty trace')
 found={};sources={}
 for f in sorted((generated/'chunks').glob('*.c')):
  text=f.read_text();functions=re.findall(r'^void (func_[0-9A-Fa-f]+)\(CPUState\* ctx\)',text,re.M)
  if len(functions)!=1:raise ValueError('expected one public chunk entry in '+str(f))
  cases={int(x,16) for x in re.findall(r'case 0x([0-9A-Fa-f]+)u: goto label_',text)}
  for address in addresses:
   if address in cases:
    if address in found:raise ValueError('ambiguous chunk ownership')
    found[address]=(f,functions[0]);sources[f.name]={'path':str(f.resolve()),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
 if set(found)!=set(addresses):raise ValueError('some trace PCs have no generated dispatcher case')
 lines=['/* PRIVATE generated trace-restricted diagnostic. NOT the complete game. */','#include "cpu/cpu.h"']
 for symbol in sorted(set(x[1] for x in found.values())):lines.append('extern void '+symbol+'(CPUState *);')
 lines+=['int mgx_generated_dispatch(CPUState *cpu,uint32_t address){','    if(address<cpu->ram_size)address|=GC_RAM_BASE;','    cpu->pc=address;','    switch(address){']
 for address,(_,symbol) in sorted(found.items()):lines.append(f'    case 0x{address:08X}u: {symbol}(cpu);return 1;')
 lines+=['    default:return 0;','    }','}']
 output.mkdir(parents=True,exist_ok=True);(output/'trace_bridge.c').write_text('\n'.join(lines)+'\n')
 result={'scope':'trace-restricted diagnostic only','trace_sha256':hashlib.sha256(trace.read_bytes()).hexdigest(),'entry_count':len(found),'chunk_count':len(sources),'chunks':sources,'unknown_dispatch_returns_failure':True,'complete_game':False,'bridge_sha256':hashlib.sha256((output/'trace_bridge.c').read_bytes()).hexdigest()}
 (output/'selection.json').write_text(json.dumps(result,indent=2)+'\n');return result
def main():
 p=argparse.ArgumentParser();p.add_argument('--generated',required=True,type=Path);p.add_argument('--trace',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();print(json.dumps(generate(a.generated,a.trace,a.output),indent=2))
if __name__=='__main__':main()
