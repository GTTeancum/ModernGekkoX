/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases,mode,index_value,alias,continued;
static const uint8_t original[]={0x60,0,0,0,0x38,0x60,0,0,0x4e,0x80,0,0x20};
static const uint32_t token=0x38600000;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"template case %u mode %u line %d: %s\n",cases,mode,__LINE__,#x);exit(1);}}while(0)
static void put(uint8_t*p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int dispatch(CPUState*c,uint32_t pc){
    (void)pc;c->pc=0x80001000;c->reserve_valid=1;c->reserve_addr=0x80002004;
    uint32_t at=(alias==0?0x80000000:alias==1?0xc0000000:0)+0x2004;
    if(mode==1)c->pc=0x80001004;
    if(mode==2)mem_write16(c,at+2,1);
    else if(mode==3)mem_write32(c,at,0x3860000f);
    else if(mode==4)mem_write32(c,at,0x3860ffff);
    else if(mode==5)mem_write32(c,at,0x38800001);
    else if(mode==6)mem_write32(c,at+4,0x60000000);
    else if(mode==7){c->ram[0x2000]=0;mem_write32(c,at,0x38600001);}
    else if(mode==8){(void)mgx_exec_template_li(c,0x80002000);}
    else if(mode==9){c->ram[0x2004]=0x39;(void)mgx_exec_template_li(c,0x80002004);}
    else {
        mem_write32(c,at,0x38600000|index_value);
        if(mode==1){++continued;return 0;}
        CHECK(!c->reserve_valid);
        c->gpr[3]=0xdeadbeef;c->cr=0xabcd1234;c->lr=0x12345678;
        c->gpr[3]=mgx_exec_template_li(c,0x80002004);
        CHECK(c->gpr[3]==index_value);CHECK(c->cr==0xabcd1234);CHECK(c->lr==0x12345678);
        CHECK(c->pc==0x80002004);
        c->pc=0x80001008;mem_write32(c,at,0x38600000);
        CHECK(memcmp(c->ram+0x2000,original,sizeof(original))==0);
        return 0;
    }
    ++continued;return 0;
}
static void one(unsigned m,unsigned val,unsigned al,unsigned invalid){
    mode=m;index_value=val;alias=al;continued=0;++cases;
    CPUState*c=calloc(1,sizeof(*c));CHECK(c!=NULL);c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram!=NULL);
    memcpy(c->ram+0x2000,original,sizeof(original));put(c->ram+0x1000,0x90040000);put(c->ram+0x1008,0x90640000);
    mgx_memory mem={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.count=1;plan.entry=0x80001000;plan.sections[0]=(mgx_dol_section){256,0x80001000,0x2000,0,0x1000,1};
    mgx_code_template t={0x80002000,sizeof(original),4,14,original,{0x80001000,0x80001008},{0x90040000,0x90640000},&token};
    uint32_t bad_token=0;uint8_t bad_original[sizeof(original)];memcpy(bad_original,original,sizeof(original));
    if(invalid==1)t.patch_offset=2;
    if(invalid==2)t.patch_offset=12;
    if(invalid==3)t.size=4097;
    if(invalid==4)t.address=0x90002000;
    if(invalid==5)t.max_immediate=32768;
    if(invalid==6)t.original=NULL;
    if(invalid==7)t.instrumentation_token=NULL;
    if(invalid==8)t.instrumentation_token=&bad_token;
    if(invalid==9)t.writer_word[0]^=1;
    if(invalid==10)t.writer_pc[0]=0x80002000;
    if(invalid==11)c->ram[0x2000]^=1;
    if(invalid==12){bad_original[4]=0x3c;memcpy(c->ram+0x2000,bad_original,sizeof(original));t.original=bad_original;}
    if(invalid==13)t.size=0;
    if(invalid==14)t.writer_pc[0]=0x80001001;
    if(invalid==15)t.writer_pc[0]=0x80005000;
    if(invalid==16){put(c->ram+0x1000,0x60000000);t.writer_word[0]=0x60000000;}
    mgx_execution r;mgx_exec_run_template(&r,c,&mem,&plan,dispatch,2,invalid==17?MGX_BOOT_STRICT:MGX_BOOT_WII_CPU,&t);
    const char*why=invalid?"invalid-template-profile":mode==8?"unregistered-template-instruction":mode==9?"invalid-template-instruction":mode?"write-to-translated-code":"untranslated-address";
    CHECK(!strcmp(r.stop.reason,why));CHECK(!continued);
    if(!invalid&&!mode){CHECK(r.template_writes==(val?2u:0u));CHECK(r.template_instruction_reads==1);}
    if(!invalid&&mode>=1&&mode<=7){CHECK(c->reserve_valid);CHECK(r.template_writes==0);CHECK(mem_read32(c,0x80002004)==token);}
    if(invalid)CHECK(r.stop.dispatches==0);
    free(c->ram);free(c);
}
int main(void){
    for(unsigned a=0;a<3;++a)for(unsigned v=0;v<15;++v)one(0,v,a,0);
    for(unsigned m=1;m<=9;++m)one(m,1,0,0);
    for(unsigned i=1;i<=17;++i)one(0,1,0,i);
    printf("PASS: %u synthetic template cases; %u checks\n",cases,checks);return 0;
}
