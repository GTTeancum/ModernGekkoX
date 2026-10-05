/* SPDX-License-Identifier: GPL-3.0-or-later
 * Scalar store policy tests only; no game data or external device behavior. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases,continued,prior_checks,prior_journals;
static uint32_t address;static uint64_t value;static uint8_t width;static int external;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL case %u line %u: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
static void old_check(CPUState*c,u32 a,u64 v,u8 n,void*u){(void)c;(void)a;(void)v;(void)n;(void)u;++prior_checks;}
static void old_journal(u32 a,u32 n,void*u){(void)a;(void)n;(void)u;++prior_journals;}
static int dispatch(CPUState*c,uint32_t pc){
    c->pc=pc+4;
    /* Reserve an alias of the affected line, including physical-address stores. */
    c->reserve_valid=true;c->reserve_addr=(address&0x1fffffffu)|0x80000000u;
    if(external)c->external_write(c,address,value,width);
    else switch(width){case 1:mem_write8(c,address,(u8)value);break;case 2:mem_write16(c,address,(u16)value);break;
        case 4:mem_write32(c,address,(u32)value);break;case 8:mem_write64(c,address,value);break;default:abort();}
    ++continued;return 0;
}
static void one(mgx_boot_profile profile,int bank,unsigned alias,unsigned n,int direct_external,
                uint32_t offset,int changed,int invalid){
    ++cases;width=(uint8_t)n;external=direct_external;
    static const uint32_t bases[]={0,0x80000000u,0xc0000000u};
    address=invalid==1?offset:bases[alias]+(bank?0x10000000u:0)+offset;
    CPUState *c=calloc(1,sizeof(*c));CHECK(c);c->ram_size=c->mem2_size=0x2000;
    c->ram=malloc(c->ram_size);c->mem2=malloc(c->mem2_size);CHECK(c->ram&&c->mem2);
    for(unsigned i=0;i<0x2000;++i)c->ram[i]=c->mem2[i]=(uint8_t)(i*19+7);
    uint8_t saved1[0x2000],saved2[0x2000];memcpy(saved1,c->ram,sizeof(saved1));memcpy(saved2,c->mem2,sizeof(saved2));
    mgx_memory m={c->ram,c->ram_size,c->mem2,c->mem2_size};mgx_dol_plan p={0};p.entry=0x80001000;p.count=2;
    p.sections[0]=(mgx_dol_section){256,0x80001000,16,0,0x1000,1};
    p.sections[1]=(mgx_dol_section){272,0x80001020,16,0,0x1020,1};
    value=0;
    if(!invalid){for(unsigned i=0;i<n;++i)value=(value<<8)|((bank?saved2:saved1)[offset+i]);
        if(changed)value^=UINT64_C(1)<<(8*(n-1));
        /* High bits are not part of a narrow external/scalar store. */
        if(n<8)value|=UINT64_C(0xde)<<56;
    }
    int overlap=!bank&&((offset+n>0x1000&&offset<0x1010)||(offset+n>0x1020&&offset<0x1030));
    int accepted=!invalid&&(!overlap||(profile==MGX_BOOT_WII_CPU&&!changed));
    prior_checks=prior_journals=continued=0;ppc_set_mem_write_check(old_check,c);ppc_set_mem_write_journal(old_journal,c);
    mgx_execution r;mgx_exec_run_profile(&r,c,&m,&p,dispatch,4,profile);
    CHECK(continued==(unsigned)accepted);CHECK(r.stop.dispatches==1);CHECK(r.stop.pc==p.entry+4);
    const char *reason=accepted?"untranslated-address":invalid==2?"invalid-memory-write":invalid==1?"unimplemented-memory-write":"write-to-translated-code";
    CHECK(!strcmp(r.stop.reason,reason));CHECK(r.identical_code_writes==(unsigned)(accepted&&overlap));
    CHECK(prior_checks==0&&prior_journals==0);CHECK(g_mem_write_check==old_check&&g_mem_write_check_user==c);
    CHECK(g_mem_write_journal==old_journal&&g_mem_write_journal_user==c);CHECK(c->reserve_valid==!accepted);
    if(accepted){uint8_t *s=(bank?saved2:saved1)+offset;for(unsigned i=0;i<n;++i)s[n-i-1]=(uint8_t)(value>>(i*8));}
    CHECK(!memcmp(saved1,c->ram,sizeof(saved1)));CHECK(!memcmp(saved2,c->mem2,sizeof(saved2)));
    if(!accepted){CHECK(r.stop.width==n);}
    if(accepted&&overlap){CHECK(r.last_identical_pc==p.entry+4);CHECK(r.last_identical_address==(address&0x0fffffff)+0x80000000u);}
    mem_write8(c,0x80001800,1);CHECK(prior_checks==1&&prior_journals==1);
    ppc_set_mem_write_check(NULL,NULL);ppc_set_mem_write_journal(NULL,NULL);cpu_free(c);free(c);
}
int main(void){
    for(int profile=0;profile<2;++profile)for(int bank=0;bank<2;++bank)for(unsigned alias=0;alias<3;++alias)
    for(unsigned n=1;n<=8;n*=2)for(int ext=0;ext<2;++ext)for(int change=0;change<2;++change)
        one((mgx_boot_profile)profile,bank,alias,n,ext,0x1004,change,0);
    /* At each edge, changing just the non-text part must reject the whole write. */
    const uint32_t starts[]={0xffc,0xfff,0x100c,0x1010,0x101c,0x102c,0x1030};
    for(unsigned j=0;j<sizeof(starts)/sizeof(starts[0]);++j)for(int change=0;change<2;++change)
        one(MGX_BOOT_WII_CPU,0,j%3,8,j%2,starts[j],change,0);
    const uint32_t bad[]={0x80001fffu,0xfffffffeu,0xcc000000u,0x10002000u};
    for(unsigned j=0;j<4;++j)one(MGX_BOOT_WII_CPU,0,0,8,1,bad[j],0,1);
    const unsigned badwidth[]={0,3,9};
    for(unsigned j=0;j<3;++j)one(MGX_BOOT_WII_CPU,0,0,badwidth[j],1,0x1004,0,2);
    printf("PASS: %u synthetic code-write cases; %u checks\n",cases,checks);return 0;
}
