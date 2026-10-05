/* SPDX-License-Identifier: GPL-3.0-or-later
 * Fixed Wii HID4 preset, not a general Broadway MMU implementation. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned which,checks,continued;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL HID4 case %u line %d: %s\n",which,__LINE__,#x);exit(1);}}while(0)
static int dispatch(CPUState*c,uint32_t at){
    mgx_execution*r=c->external_user_data;(void)at;
    if(which==0){
        CHECK(c->runtime_cpu==PPC_RUNTIME_BROADWAY);
        CHECK(ppc_mfspr(c,1011,0x80001000)==MGX_HID4_WII_PRESET);
        for(unsigned i=0;i<3;++i)ppc_mtspr(c,1011,MGX_HID4_WII_PRESET,0x80001004);
        CHECK(ppc_mfspr(c,1011,0x80001008)==MGX_HID4_WII_PRESET);
        /* Fixed aliases must still refer to the same backed storage. */
        mem_write32(c,0x80002000,0x12345678);CHECK(mem_read32(c,0xc0002000)==0x12345678);
        CHECK(mem_read32(c,0x00002000)==0x12345678);
        mem_write32(c,0x90000020,0x89abcdef);CHECK(mem_read32(c,0xd0000020)==0x89abcdef);
        CHECK(mem_read32(c,0x10000020)==0x89abcdef);
        CHECK(r->cache_events[0]==0&&r->l2_writes==0);return 0;
    }
    if(which>=1&&which<=32)ppc_mtspr(c,1011,MGX_HID4_WII_PRESET^(1u<<(which-1)),0x80001004);
    else if(which==33){c->msr|=0x4000;ppc_mtspr(c,1011,MGX_HID4_WII_PRESET,0x80001004);CHECK(c->program_exception==PPC_PROGRAM_PRIV);return 1;}
    else if(which==34){c->msr|=0x4000;(void)ppc_mfspr(c,1011,0x80001004);CHECK(c->program_exception==PPC_PROGRAM_PRIV);return 1;}
    else if(which==35){c->runtime_cpu=PPC_RUNTIME_BROADWAY;ppc_mtspr(c,1011,MGX_HID4_WII_PRESET,0x80001004);}
    else if(which==36){c->runtime_cpu=PPC_RUNTIME_BROADWAY;(void)ppc_mfspr(c,1011,0x80001004);}
    else if(which==37){ppc_mtspr(c,1011,0,0x80001004);CHECK(c->program_exception==PPC_PROGRAM_ILLEGAL);return 1;}
    else if(which==38){r->hid4=0;ppc_mtspr(c,1011,MGX_HID4_WII_PRESET,0x80001004);}
    else if(which==39){ppc_mtspr(c,1011,MGX_HID4_WII_PRESET,0x80001004);mem_read32(c,0x90008000);}
    ++continued;return 1;
}
int main(void){
 for(which=0;which<40;++which){CPUState*c=calloc(1,sizeof(*c));CHECK(c!=NULL);c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);c->mem2_size=0x4000;c->mem2=calloc(1,c->mem2_size);CHECK(c->ram&&c->mem2);
  mgx_memory m={c->ram,c->ram_size,c->mem2,c->mem2_size};mgx_dol_plan p;memset(&p,0,sizeof(p));p.count=1;p.entry=0x80001000;p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
  mgx_execution r;continued=0;mgx_boot_profile profile=(which>=35&&which<=37)?MGX_BOOT_STRICT:MGX_BOOT_WII_CPU;
  mgx_exec_run_profile(&r,c,&m,&p,dispatch,2,profile);
  const char* want=which==0?"untranslated-address":which<=32?"unsupported-hid4-transition":which<=34?"guest-exception":which==35?"unimplemented-spr-write":which==36?"unimplemented-spr-read":which==37?"guest-exception":which==38?"unsupported-hid4-transition":"unimplemented-memory-read";
  CHECK(!strcmp(r.stop.reason,want));CHECK(!continued);
  CHECK(r.hid4==((which>=35&&which<=38)?0:MGX_HID4_WII_PRESET));
  CHECK(r.hid4_writes==(which==0?3u:which==39?1u:0u));CHECK(r.hid4_reads==(which==0?2u:0u));
  if(which>=1&&which<=32){CHECK(r.stop.pc==0x80001004);CHECK(r.stop.value==(MGX_HID4_WII_PRESET^(1u<<(which-1))));}
  if(which==39)CHECK(r.stop.address==0x90008000);
  free(c->ram);free(c->mem2);free(c);
 }
 printf("PASS: 40 synthetic HID4 cases; %u checks\n",checks);return 0;
}
