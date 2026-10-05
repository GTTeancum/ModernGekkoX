#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect a linked nxdk PE32 image; report a static lower bound, not free RAM."""
import argparse,json,struct
from pathlib import Path

def audit(path,mem1_bytes=24*1024*1024,physical_bytes=64*1024*1024):
    if mem1_bytes<0 or physical_bytes<=0:raise ValueError('invalid memory budget')
    b=Path(path).read_bytes()
    def u16(at):return struct.unpack_from('<H',b,at)[0]
    def u32(at):return struct.unpack_from('<I',b,at)[0]
    if len(b)<64 or b[:2]!=b'MZ':raise ValueError('invalid DOS header')
    p=u32(60)
    if p+24>len(b) or b[p:p+4]!=b'PE\0\0' or u16(p+4)!=0x14c:raise ValueError('expected I386 PE')
    n=u16(p+6);optional=u16(p+20);h=p+24
    if not n or optional<96 or h+optional+n*40>len(b) or u16(h)!=0x10b:raise ValueError('invalid PE32 headers')
    alignment=u32(h+32);image=u32(h+56);headers=u32(h+60)
    if alignment<4096 or alignment&(alignment-1) or image%alignment or not 0<headers<=image:raise ValueError('invalid image alignment/size')
    sections=[];ranges=[]
    for i in range(n):
        at=h+optional+i*40;name=b[at:at+8].rstrip(b'\0').decode('ascii','replace')
        virtual,rva,raw,offset=struct.unpack_from('<4I',b,at+8);flags=u32(at+36)
        extent=max(virtual,raw);pages=(extent+alignment-1)//alignment*alignment
        if rva%alignment or rva<headers or rva+pages>image:raise ValueError('section outside image')
        if raw and (offset<headers or offset+raw>len(b)):raise ValueError('section outside file')
        for lo,hi in ranges:
            if rva<hi and rva+pages>lo:raise ValueError('overlapping sections')
        ranges.append((rva,rva+pages))
        sections.append({'name':name,'rva':rva,'virtual_bytes':virtual,'file_bytes':raw,
                         'mapped_bytes':pages,'executable':bool(flags&0x20000000),
                         'writable':bool(flags&0x80000000)})
    minimum=image+mem1_bytes
    return {'pe_image_bytes':image,'sections':sections,'guest_mem1_bytes':mem1_bytes,
            'physical_baseline_bytes':physical_bytes,'image_plus_mem1_bytes':minimum,
            'minimum_over_budget_bytes':max(0,minimum-physical_bytes),'lower_bound_only':True,
            'excluded':['kernel/SDK runtime allocations','framebuffers/GPU allocations','heap/stack',
                        'CPU/execution state','MEM2','asset buffers','fragmentation'],
            'hardware_measured':False,'fits_hardware':None}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path)
    p.add_argument('--mem1-bytes',type=int,default=24*1024*1024)
    p.add_argument('--physical-bytes',type=int,default=64*1024*1024)
    p.add_argument('--output',type=Path);a=p.parse_args()
    result=json.dumps(audit(a.image,a.mem1_bytes,a.physical_bytes),indent=2)+'\n'
    if a.output:a.output.write_text(result)
    else:print(result,end='')
if __name__=='__main__':main()
