#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test native memory-query glue using a synthetic kernel response, not hardware."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class MemoryGlue(unittest.TestCase):
 def test_query_and_budget(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'xboxkrnl').mkdir()
   (p/'xboxkrnl/xboxkrnl.h').write_text('''#include <stdint.h>
typedef int32_t NTSTATUS;
#define NT_SUCCESS(x) ((x)>=0)
typedef struct {uint32_t Length,TotalPhysicalPages,AvailablePages,VirtualMemoryBytesCommitted,VirtualMemoryBytesReserved,CachePagesCommitted,PoolPagesCommitted,StackPagesCommitted,ImagePagesCommitted;} MM_STATISTICS;
NTSTATUS MmQueryStatistics(MM_STATISTICS*);
''')
   (p/'test.c').write_text('''#define NXDK 1
#include "mgx_nxdk_memory.h"
#include <assert.h>
#include <string.h>
static NTSTATUS status;static MM_STATISTICS response;
NTSTATUS MmQueryStatistics(MM_STATISTICS*s){assert(s->Length==sizeof(*s));*s=response;return status;}
int main(void){
 mgx_xbox_memory out={0},before={0};
 response.TotalPhysicalPages=16384;response.AvailablePages=6144;response.ImagePagesCommitted=8000;
 assert(!mgx_nxdk_query_memory(0));assert(mgx_nxdk_query_memory(&out));
 assert(out.total_pages==16384 && out.available_pages==6144 && out.image_pages==8000);
 assert(mgx_xbox_memory_can_fit(&out,25165824));assert(!mgx_xbox_memory_can_fit(&out,25165825));
 assert(!mgx_xbox_memory_can_fit(&out,UINT64_MAX));assert(!mgx_xbox_memory_can_fit(0,1));
 before=out;status=-1;assert(!mgx_nxdk_query_memory(&out));assert(!memcmp(&before,&out,sizeof out));
 status=0;response.AvailablePages=16385;assert(!mgx_nxdk_query_memory(&out));assert(!memcmp(&before,&out,sizeof out));
 response.AvailablePages=0;response.ImagePagesCommitted=16385;assert(!mgx_nxdk_query_memory(&out));
 response.ImagePagesCommitted=0;assert(mgx_nxdk_query_memory(&out));assert(!mgx_xbox_memory_can_fit(&out,1));
 out.available_pages=out.total_pages+1;assert(!mgx_xbox_memory_can_fit(&out,0));return 0;
}''')
   subprocess.run(['clang','-std=c11','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(ROOT/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True,capture_output=True,text=True)
   subprocess.run([str(p/'test')],check=True,capture_output=True,text=True)
if __name__=='__main__':unittest.main()
