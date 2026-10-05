/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic CPU/register/cache tests. No game addresses or instructions. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned which,checks,continued;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL case %u line %u: %s\n",which,__LINE__,#x);exit(1);}}while(0)
static int dispatch(CPUState *c,uint32_t address){
    mgx_execution *r=(mgx_execution *)c->external_user_data;
    (void)address;
    switch(which){
    case 0:
        CHECK(c->msr==0x2032 && c->hid2==0xe0000000);
        CHECK(ppc_mfspr(c,1008,0x80001000)==0x0011c664);
        ppc_mtspr(c,1008,0x0011ce64,0x80001004);
        CHECK(ppc_mfspr(c,1008,0x80001008)==0x0011c664);
        CHECK(r->icache_invalidations==1 && r->hid0_writes==1);
        ppc_mtspr(c,1008,0x00110664,0x8000100c);
        CHECK(ppc_mfspr(c,1008,0x80001010)==0x00110664);
        ppc_mtspr(c,1008,0x0011c664,0x80001014);
        CHECK(ppc_mfspr(c,1008,0x80001018)==0x0011c664);return 0;
    case 1: ppc_mtspr(c,1008,0x0011c264,0x80001004);break; /* DCFI change */
    case 2: ppc_mtspr(c,1008,0x0011d664,0x80001004);break; /* DLOCK */
    case 3: ppc_mfspr(c,1009,0x80001004);break;
    case 4: ppc_mtspr(c,1009,1,0x80001004);break;
    case 5:
        c->msr|=0x4000;(void)ppc_mfspr(c,1008,0x80001004);
        CHECK(c->exception==PPC_EXC_PROGRAM && r->hid0_reads==0);return 1;
    case 6:
        c->msr|=0x4000;ppc_mtspr(c,1008,0,0x80001004);
        CHECK(c->exception==PPC_EXC_PROGRAM && r->hid0_writes==0);return 1;
    case 7:
        c->ram[0x2020]=0x5a;
        ppc_cache_control(c,PPC_CACHE_DCBST,0x80002023,0x80001004);
        ppc_cache_control(c,PPC_CACHE_DCBF,0xc000202f,0x80001008);
        ppc_cache_control(c,PPC_CACHE_ICBI,0x1000,0x8000100c);
        CHECK(c->ram[0x2020]==0x5a && r->cache_events[0]==1 &&
              r->cache_events[1]==1 && r->cache_events[3]==1);return 0;
    case 8: ppc_cache_control(c,PPC_CACHE_DCBI,0x80002000,0x80001004);break;
    case 9:
        for(unsigned i=0;i<512;++i)
            ppc_cache_control(c,PPC_CACHE_DCBI,0xe0000000+i*32,0x80001004);
        CHECK(r->locked_cache_invalidations==512&&r->cache_events[PPC_CACHE_DCBI]==512);return 0;
    case 10:
        c->locked_cache_valid[0]=true;c->locked_cache_tag[0]=0x1234;
        ppc_cache_control(c,PPC_CACHE_DCBI,0xe0000000,0x80001004);break;
    case 11: ppc_cache_control(c,PPC_CACHE_DCBF,0xcc008000,0x80001004);break;
    case 12:
        c->msr|=0x4000;ppc_cache_control(c,PPC_CACHE_DCBI,0xe0000000,0x80001004);
        CHECK(c->exception==PPC_EXC_PROGRAM&&r->locked_cache_invalidations==0);return 1;
    case 13:
        CHECK(ppc_mfspr(c,1017,0x80001000)==0);
        ppc_mtspr(c,1017,0x00200001,0x80001004);
        CHECK(ppc_mfspr(c,1017,0x80001008)==0x00200000&&r->l2_invalidations==1);
        ppc_mtspr(c,1017,0x00200000,0x8000100c);
        CHECK(r->l2_invalidations==1); /* repeat request is not a new edge */
        ppc_mtspr(c,1017,0,0x80001010);
        ppc_mtspr(c,1017,0x80000000,0x80001014);
        CHECK(ppc_mfspr(c,1017,0x80001018)==0x80000000);return 0;
    case 14: ppc_mtspr(c,1017,0x80200000,0x80001004);break;
    case 15: ppc_mtspr(c,1017,0x00008000,0x80001004);break;
    case 16: mem_write32(c,0xc0001004,0x12345678);break;
    }
    ++continued;return 1;
}
int main(void){
    const char *reasons[]={"untranslated-address","unsupported-hid0-transition","unsupported-hid0-transition","unimplemented-spr-read","unimplemented-spr-write","guest-exception","guest-exception","untranslated-address","unsupported-data-cache-invalidate","untranslated-address","unsupported-live-locked-cache-invalidate","unsupported-cache-address","guest-exception","untranslated-address","invalid-l2-invalidate-while-enabled","unsupported-l2cr-transition","write-to-translated-code"};
    for(which=0;which<17;++which){
        CPUState c;memset(&c,0,sizeof(c));c.ram_size=0x4000;c.ram=calloc(1,c.ram_size);CHECK(c.ram);
        mgx_memory m={c.ram,c.ram_size,NULL,0};mgx_dol_plan p;memset(&p,0,sizeof(p));
        p.count=1;p.entry=0x80001000;p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
        mgx_execution r;continued=0;
        mgx_exec_run_profile(&r,&c,&m,&p,dispatch,2,MGX_BOOT_WII_CPU);
        CHECK(!strcmp(r.stop.reason,reasons[which]));CHECK(continued==0);CHECK(r.stop.dispatches==1);
        if(which==1||which==2)CHECK(r.hid0==0x0011c664&&r.hid0_writes==0);
        if(which==10)CHECK(c.locked_cache_valid[0]&&c.locked_cache_tag[0]==0x1234);
        if(which==14||which==15)CHECK(r.l2cr==0&&r.l2_writes==0);
        if(which==16)CHECK(mem_read32(&c,0x80001004)==0);
        free(c.ram);
    }
    printf("PASS: 17 synthetic boot/cache cases; %u checks\n",checks);return 0;
}
