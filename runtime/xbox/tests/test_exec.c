/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic data only. Tests stops, endian aliases, code-write protection and
 * diagnostic state cleanup; does not assert any Wii device is implemented. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern unsigned dolrecomp_call_depth;
static unsigned test_id,calls,continued,old_writes,checks;
static void prior_journal(uint32_t off,uint32_t width,void *user){(void)off;(void)width;(void)user;++old_writes;}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL case %u line %u: %s\n",test_id,__LINE__,#x);exit(1);}}while(0)
static int dispatch(CPUState *c,uint32_t address){
    ++calls;
    switch(test_id){
    case 0: c->pc=address+4;return 1;
    case 1: return 0;
    case 2: c->exception=1;return 1;
    case 3: (void)c->external_read(c,0xcc000000u,4);break;
    case 4: c->external_write(c,0xcd000000u,0x12345678,4);break;
    case 5: c->spr_read(c,1008,0x80001008);break;
    case 6: c->spr_write(c,1008,123,0x8000100c);break;
    case 7: c->instruction_fallback(c,0xdeadbeef,0x80001010);break;
    case 8: c->cache_control(c,3,0x80002000,0x80001014);break;
    case 9: (void)c->external_pointer(c,0xcc000000,16);break;
    case 10: c->external_read32(c,0xcc000000,1);break;
    case 11: c->external_write32(c,0xcc000000,0xfeedface,2);break;
    case 12: mem_write32(c,0x80001004,0x11223344);break;
    case 13: mem_write32(c,0x00001004,0x11223344);break;
    case 14: mem_write32(c,0xc0001004,0x11223344);break;
    case 15:
        c->external_write(c,0x2004,UINT64_C(0x123456789abcdef0),8);
        CHECK(mem_read32(c,0x80002004)==0x12345678);
        CHECK(mem_read32(c,0xc0002008)==0x9abcdef0);
        CHECK(c->external_read(c,0x2004,8)==UINT64_C(0x123456789abcdef0));
        CHECK(c->ram[0x2004]==0x12 && c->ram[0x200b]==0xf0);
        return 0;
    case 16:
        c->external_write(c,0x10000008,0x1234,2);
        CHECK(mem_read16(c,0x90000008)==0x1234);
        CHECK(mem_read16(c,0xd0000008)==0x1234);
        return 0;
    case 17: dolrecomp_call_depth=17;c->instruction_fallback(c,0,0x80001010);break;
    }
    ++continued;return 1;
}
int main(void){
    static const char *reasons[]={"dispatch-budget-exhausted","untranslated-address","guest-exception","unimplemented-memory-read","unimplemented-memory-write","unimplemented-spr-read","unimplemented-spr-write","unimplemented-instruction","unimplemented-cache-control","unimplemented-external-pointer","unimplemented-external-control-read","unimplemented-external-control-write","write-to-translated-code","write-to-translated-code","write-to-translated-code","untranslated-address","untranslated-address","unimplemented-instruction"};
    unsigned total=0;
    for(test_id=0;test_id<18;++test_id){
        CPUState c;memset(&c,0,sizeof(c));c.ram_size=0x4000;c.mem2_size=0x1000;
        c.ram=calloc(1,c.ram_size);c.mem2=calloc(1,c.mem2_size);CHECK(c.ram&&c.mem2);
        mgx_memory memory={c.ram,c.ram_size,c.mem2,c.mem2_size};mgx_execution r;
        mgx_dol_plan plan;memset(&plan,0,sizeof(plan));plan.count=1;plan.entry=0x80001000;
        plan.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
        calls=continued=old_writes=0;dolrecomp_call_depth=5;
        ppc_set_mem_write_journal(prior_journal,&c);
        mgx_exec_run(&r,&c,&memory,&plan,dispatch,4);
        CHECK(!strcmp(r.stop.reason,reasons[test_id]));CHECK(continued==0);
        CHECK(calls==(test_id==0?4u:1u));CHECK(r.stop.dispatches==calls);
        CHECK(g_mem_write_journal==prior_journal&&g_mem_write_journal_user==&c);
        CHECK(dolrecomp_call_depth==5 && old_writes==0);
        if(test_id==0){CHECK(r.stop.pc==0x80001010);CHECK(r.stop.trace_count==4);}
        if(test_id==4){CHECK(r.stop.address==0xcd000000 && r.stop.value==0x12345678);}
        if(test_id==5){CHECK(r.stop.pc==0x80001008 && r.stop.address==1008);}
        if(test_id==7){CHECK(r.stop.raw==0xdeadbeef && r.stop.pc==0x80001010);}
        if(test_id>=12&&test_id<=14){CHECK(mem_read32(&c,0x80001004)==0);}
        ppc_set_mem_write_journal(NULL,NULL);free(c.ram);free(c.mem2);++total;
    }
    CPUState c;memset(&c,0,sizeof(c));mgx_memory m={0};mgx_dol_plan p={0};mgx_execution r;
    calls=0;
    mgx_exec_run(&r,NULL,&m,&p,dispatch,1);CHECK(!strcmp(r.stop.reason,"invalid-execution-arguments"));++total;
    mgx_exec_run(&r,&c,&m,&p,dispatch,0);CHECK(!strcmp(r.stop.reason,"invalid-execution-arguments"));++total;
    m.mem1_size=1;mgx_exec_run(&r,&c,&m,&p,dispatch,1);CHECK(!strcmp(r.stop.reason,"invalid-execution-arguments"));++total;
    m.mem1_size=0;p.count=19;mgx_exec_run(&r,&c,&m,&p,dispatch,1);CHECK(!strcmp(r.stop.reason,"invalid-execution-arguments"));++total;
    p.count=1;p.sections[0].bank=1;p.sections[0].executable=1;
    mgx_exec_run(&r,&c,&m,&p,dispatch,1);CHECK(!strcmp(r.stop.reason,"invalid-execution-arguments"));++total;
    CHECK(calls==0);printf("PASS: %u synthetic execution cases; %u checks\n",total,checks);return 0;
}
