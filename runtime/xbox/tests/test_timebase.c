/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,continued,journaled,checked,old_called;
static CPUState original,saved_cpu;
static mgx_execution saved_run;
static u8 saved_ram[0x4000];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"clock-runtime case %u mode %u line %u: %s\n",cases,mode,__LINE__,#x);exit(1);}}while(0)
static void old_clock(CPUState*c,u16 reg,bool w,u32 v,u32 cia){(void)c;(void)reg;(void)w;(void)v;(void)cia;++old_called;}
static void old_check(CPUState*c,u32 a,u64 v,u8 w,void*u){(void)c;(void)a;(void)v;(void)w;CHECK(u==&original);++checked;}
static void old_journal(u32 a,u32 n,void*u){(void)a;(void)n;CHECK(u==&original);++journaled;}
static u64 old_read(CPUState*c,u32 a,u8 w){(void)c;(void)a;(void)w;return 0;}
static void old_write(CPUState*c,u32 a,u64 v,u8 w){(void)c;(void)a;(void)v;(void)w;}
static u32 old_read32(CPUState*c,u32 a,u8 rid){(void)c;(void)a;(void)rid;++old_called;return 0;}
static void old_write32(CPUState*c,u32 a,u32 v,u8 rid){(void)c;(void)a;(void)v;(void)rid;++old_called;}
static void *old_pointer(CPUState*c,u32 a,u32 n){(void)c;(void)a;(void)n;++old_called;return NULL;}
static void old_fallback(CPUState*c,u32 raw,u32 cia){(void)c;(void)raw;(void)cia;++old_called;}
static bool old_host(CPUState*c,u32 a){(void)c;(void)a;++old_called;return false;}
static u32 old_spr_read(CPUState*c,u16 reg,u32 cia){(void)c;(void)reg;(void)cia;++old_called;return 0;}
static void old_spr_write(CPUState*c,u16 reg,u32 v,u32 cia){(void)c;(void)reg;(void)v;(void)cia;++old_called;}
static void old_cache(CPUState*c,u8 op,u32 a,u32 cia){(void)c;(void)op;(void)a;(void)cia;++old_called;}
static void restore_expected_callbacks(CPUState*c){
    c->timebase_access=original.timebase_access;c->external_user_data=original.external_user_data;
    c->external_read=original.external_read;c->external_write=original.external_write;
    c->external_read32=original.external_read32;c->external_write32=original.external_write32;
    c->external_pointer=original.external_pointer;c->instruction_fallback=original.instruction_fallback;
    c->host_call=original.host_call;c->spr_read=original.spr_read;c->spr_write=original.spr_write;c->cache_control=original.cache_control;
}
static int dispatch(CPUState*c,uint32_t pc){
    (void)pc;mgx_execution*r=c->external_user_data;
    if(mode==4){PPCTimebaseAccess hook=c->timebase_access;cpu_reset(c);CHECK(c->timebase_access==hook && c->external_user_data==r);}
    for(unsigned i=0;i<32;++i)c->gpr[i]=0xace00000u+i;
    c->timebase=UINT64_C(0x123456789abcdef0);c->reserve_valid=true;c->reserve_addr=0x80003020;
    c->cr=0x12345678;c->xer=0xa0000000;c->lr=0x80005544;c->pc=0x80001004;
    memset(c->ram+0x2000,0x5a,0x100);
    /* Populate counters and journal history; rejection must not erase or append. */
    r->irq.event_count=1;r->irq.events[0]=(mgx_mmio_event){0x80000ffc,0xcc003000,0x10100,4,0};r->irq.reads=1;
    r->serial.output_bytes=1;r->serial.output[0]='Z';
    if(mode==7 || mode==8 || mode==11)c->msr|=0x4000;
    saved_cpu=*c;saved_run=*r;memcpy(saved_ram,c->ram,sizeof(saved_ram));
    switch(mode){
    case 0:case 4:c->gpr[3]=ppc_mftb(c,268,0x80002340);break;
    case 1:c->gpr[3]=ppc_mftb(c,269,0x80002344);break;
    case 2:ppc_mtspr(c,284,0xfeedface,0x80002348);break;
    case 3:ppc_mtspr(c,285,0xfeedface,0x8000234c);break;
    case 5:(void)ppc_mftb(c,270,0x80002354);return 1;
    case 6:c->gpr[3]=ppc_mfspr(c,268,0x80002358);break;
    case 7:ppc_mtspr(c,284,0xfeedface,0x8000235c);return 1;
    case 8:c->gpr[3]=ppc_mftb(c,269,0x80002360);break;
    case 9:return 0;
    case 10:c->pc=0x80001008;return 1;
    case 11:c->gpr[3]=ppc_mfspr(c,269,0x8000236c);break;
    case 12:(void)ppc_mfspr(c,284,0x80002370);return 1;
    }
    ++continued;mem_write32(c,0x80003020,0xbad);return 0;
}
static void one(unsigned m,mgx_boot_profile profile,unsigned supplied){
    ++cases;mode=m;continued=journaled=checked=old_called=0;
    CPUState*c=calloc(1,sizeof(*c));mgx_execution*r=calloc(1,sizeof(*r));CHECK(c&&r);
    c->ram_size=sizeof(saved_ram);c->ram=calloc(1,c->ram_size);CHECK(c->ram);
    if(supplied){c->timebase_access=old_clock;c->external_user_data=&original;c->external_read=old_read;c->external_write=old_write;
        c->external_read32=old_read32;c->external_write32=old_write32;c->external_pointer=old_pointer;
        c->instruction_fallback=old_fallback;c->host_call=old_host;c->spr_read=old_spr_read;
        c->spr_write=old_spr_write;c->cache_control=old_cache;}
    original=*c;
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.entry=0x80001000;
    ppc_set_mem_write_journal(old_journal,&original);ppc_set_mem_write_check(old_check,&original);
    for(unsigned repeat=0;repeat<2;++repeat){
        mgx_exec_run_profile(r,c,&memory,&plan,dispatch,1,profile);
        CHECK(!continued && !journaled && !checked && !old_called);
        CHECK(c->timebase_access==original.timebase_access && c->external_user_data==original.external_user_data);
        CHECK(c->external_read==original.external_read && c->external_write==original.external_write);
        CHECK(c->external_read32==original.external_read32 && c->external_write32==original.external_write32 && c->external_pointer==original.external_pointer && c->instruction_fallback==original.instruction_fallback);
        CHECK(c->host_call==original.host_call && c->spr_read==original.spr_read && c->spr_write==original.spr_write && c->cache_control==original.cache_control);
        CHECK(g_mem_write_check==old_check && g_mem_write_check_user==&original);
        CHECK(g_mem_write_journal==old_journal && g_mem_write_journal_user==&original);
        CHECK(!memcmp(c->ram,saved_ram,sizeof(saved_ram)));
        CHECK(!memcmp((char*)r+offsetof(mgx_execution,profile),(char*)&saved_run+offsetof(mgx_execution,profile),offsetof(mgx_execution,escape)-offsetof(mgx_execution,profile)));
        if(m<=4 || m==6 || m==8 || m==11){
            CHECK(!strcmp(r->stop.reason,m==2||m==3?"unimplemented-timebase-write":"unimplemented-timebase-read"));
            CHECK(r->stop.address==(m==0||m==4||m==6?268:m==1||m==8||m==11?269:m==2?284:285));
            CHECK(r->stop.pc==0x80002340+(m==4?0:4*m) && r->stop.width==4 && !r->stop.raw);
            CHECK(r->stop.value==(m==2||m==3?0xfeedface:0) && !c->exception);
            saved_cpu.pc=r->stop.pc;restore_expected_callbacks(&saved_cpu);
            CHECK(!memcmp(c,&saved_cpu,sizeof(*c)));
        }else if(m<9 || m==12){
            CHECK(!strcmp(r->stop.reason,"guest-exception"));
            CHECK(c->program_exception==(m==7?PPC_PROGRAM_PRIV:PPC_PROGRAM_ILLEGAL));
            CHECK(c->timebase==saved_cpu.timebase && c->reserve_valid && c->reserve_addr==saved_cpu.reserve_addr);
            CHECK(!memcmp(c->gpr,saved_cpu.gpr,sizeof(c->gpr)));
        }else CHECK(!strcmp(r->stop.reason,m==9?"untranslated-address":"dispatch-budget-exhausted"));
    }
    ppc_set_mem_write_journal(NULL,NULL);ppc_set_mem_write_check(NULL,NULL);free(c->ram);free(c);free(r);
}
int main(void){
    for(unsigned profile=MGX_BOOT_STRICT;profile<=MGX_BOOT_WII_SI_POLL_DORMANT;++profile)
    for(unsigned m=0;m<=12;++m)for(unsigned supplied=0;supplied<2;++supplied)one(m,(mgx_boot_profile)profile,supplied);
    printf("PASS: %u synthetic timebase cases; %u checks\n",cases,checks);return 0;
}
