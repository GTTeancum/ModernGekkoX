/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic register and runtime-boundary tests; no game-specific addresses. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases,mode,continued,which_alias,param;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"IRQ case %u mode %u line %d: %s\n",cases,mode,__LINE__,#x);exit(1);} } while(0)
static uint32_t addr(uint32_t p){return p+(which_alias?0xc0000000u:0);}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;
    c->pc=0x80001000;c->reserve_valid=1;c->reserve_addr=0x80002000;
    c->cr=0x87654321;c->xer=0xe0000000;c->gpr[8]=0xabcdef01;
    switch(mode){
    case 0:
        CHECK(r->irq.pi_cause==0x10100 && r->irq.pi_mask==0 && r->irq.ppc_mask==0x40000000);
        CHECK(mem_read32(c,addr(0x0c003000))==0x10100);
        CHECK(mem_read16(c,addr(0x0c003000))==1);
        CHECK(mem_read16(c,addr(0x0c003002))==0x100);
        mem_write32(c,addr(0x0c003004),0xf0);
        CHECK(mem_read32(c,addr(0x0c003004))==0xf0);
        CHECK(mem_read16(c,addr(0x0c003004))==0);
        CHECK(mem_read16(c,addr(0x0c003006))==0xf0);
        mem_write32(c,addr(0x0d000034),0x40000000);
        mem_write32(c,addr(0x0d000030),0x40000000);
        mem_write16(c,addr(0x0c00401c),0);
        CHECK(mem_read16(c,addr(0x0c00401c))==0);
        CHECK(r->irq.pi_pending==0 && r->irq.ppc_flags==0 && r->irq.mi_mask==0);
        CHECK(r->irq.reads==7 && r->irq.writes==4 && r->irq.event_count==11);
        CHECK(c->reserve_valid && c->cr==0x87654321 && c->xer==0xe0000000 && c->gpr[8]==0xabcdef01);
        return 0;
    case 1: /* Each non-pending supported mask bit roundtrips. */
        mem_write32(c,addr(0x0c003004),1u<<param);
        CHECK(mem_read32(c,addr(0x0c003004))==(1u<<param));
        mem_write32(c,addr(0x0c003004),0);CHECK(r->irq.pi_pending==0);return 0;
    case 2: /* Valid write commits, but no handler or following store is run. */
        if(param)c->msr|=0x8000;
        mem_write32(c,addr(0x0c003004),0x100);break;
    case 3: /* Acknowledge latched VI without erasing the reset-button state. */
        mem_write32(c,addr(0x0c003000),0x100);
        CHECK(mem_read32(c,addr(0x0c003000))==0x10000);
        mem_write32(c,addr(0x0c003004),0x7fff);
        CHECK(r->irq.pi_pending==0);return 0;
    case 4:mem_write32(c,addr(0x0c003004),1u<<param);break;
    case 5:mem_write32(c,addr(0x0c003000),1u<<param);break;
    case 6:c->external_write(c,addr(0x0c003004),UINT64_C(0x100000000),4);break;
    case 7:c->external_write(c,addr(0x0c003004),0xf0,param);break;
    case 8:c->external_read(c,addr(0x0c003004),param);break;
    case 9:c->external_read(c,addr(0x0c003001),param);break;
    case 10:c->external_write(c,addr(0x0c003005),0xf0,4);break;
    case 11:mem_read32(c,addr(0x0d000030+param));break;
    case 12:c->external_write(c,addr(0x0d000034),param,4);break;
    case 13:c->external_write(c,addr(0x0d000030),param,4);break;
    case 14:r->irq.ppc_flags=0x40000000;mem_write32(c,addr(0x0d000034),0x40000000);break;
    case 15:mem_write16(c,addr(0x0c00401c),param);break;
    case 16:c->external_write(c,addr(0x0c00401c),0,param);break;
    case 17:c->external_read(c,addr(0x0c00401c),param);break;
    case 18:c->external_write(c,addr(0x0c00401d),0,2);break;
    case 19:mem_write32(c,param,0);break;
    case 20:c->external_pointer(c,addr(0x0c003004),4);break;
    case 21:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)mem_write32(c,addr(0x0c003004),0xf0);
        mem_write32(c,addr(0x0c003004),0x80);break;
    case 22:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,addr(0x0c003000));
        (void)mem_read32(c,addr(0x0c003000));break;
    case 23:mem_write32(c,addr(0x0c003004),0xf0);break; /* Old CPU/strict profiles. */
    case 24:mem_write32(c,0x80001004,0x12345678);break; /* Text still protected. */
    case 25:r->irq.mi_mask=1;mem_write16(c,addr(0x0c00401c),0);break;
    case 26:c->external_write(c,addr(0x0d000034),0x40000000,param);break;
    }
    ++continued;mem_write32(c,0x80002000,0x12345678);return 0;
}
static void one(unsigned m,unsigned p,unsigned alias,mgx_boot_profile profile,const char *reason){
    ++cases;mode=m;param=p;which_alias=alias;continued=0;
    CPUState *c=calloc(1,sizeof(*c));CHECK(c!=NULL);c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram!=NULL);
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.count=1;plan.entry=0x80001000;
    plan.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_execution *r=calloc(1,sizeof(*r));CHECK(r!=NULL);
    mgx_exec_run_profile(r,c,&memory,&plan,dispatch,2,profile);
    if(strcmp(r->stop.reason,reason))fprintf(stderr,"actual reason: %s expected: %s\n",r->stop.reason,reason);
    CHECK(!strcmp(r->stop.reason,reason));CHECK(!continued);CHECK(c->exception==0);CHECK(c->reserve_valid);
    CHECK(mem_read32(c,0x80002000)==0);CHECK(mem_read32(c,0x80001004)==0);
    if(strcmp(reason,"untranslated-address"))CHECK(r->stop.pc==0x80001000);
    if(m==2){CHECK(r->irq.pi_mask==0x100 && r->irq.pi_pending==0x100);CHECK(r->irq.writes==1);}
    else if(m==21){CHECK(r->irq.pi_mask==0xf0);CHECK(r->irq.event_count==MGX_MMIO_TRACE_CAPACITY);}
    else if(m==22){CHECK(r->irq.pi_mask==0);CHECK(r->irq.reads==MGX_MMIO_TRACE_CAPACITY);}
    else if(strcmp(reason,"untranslated-address")){
        CHECK(r->irq.pi_mask==0);CHECK(r->irq.reads==0 && r->irq.writes==0 && r->irq.event_count==0);
        if(profile==MGX_BOOT_WII_IRQ)CHECK(r->irq.pi_cause==0x10100 && r->irq.ppc_mask==0x40000000);
    }
    for(unsigned i=0;i<r->irq.event_count;++i){CHECK(r->irq.events[i].pc==0x80001000);CHECK(r->irq.events[i].width==2 || r->irq.events[i].width==4);}
    free(r);free(c->ram);free(c);
}
#define RUN(m,p,why) one(m,p,a,MGX_BOOT_WII_IRQ,why)
int main(void){
    for(unsigned a=0;a<2;++a){
        RUN(0,0,"untranslated-address");RUN(3,0,"untranslated-address");
        for(unsigned p=0;p<15;++p)if(p!=8)RUN(1,p,"untranslated-address");
        for(unsigned p=0;p<2;++p)RUN(2,p,"unsupported-pending-pi-interrupt");
        for(unsigned p=15;p<32;++p){RUN(4,p,"unsupported-pi-irq-bits");RUN(5,p,"unsupported-pi-irq-bits");}
        RUN(6,0,"unsupported-pi-irq-bits");
        unsigned widths[]={0,1,2,3,8};
        for(unsigned i=0;i<5;++i){RUN(7,widths[i],"unsupported-mmio-width");RUN(26,widths[i],"unsupported-mmio-width");}
        for(unsigned i=0;i<5;++i)if(widths[i]!=2)RUN(8,widths[i],"unsupported-mmio-width");
        RUN(9,2,"unsupported-mmio-width");RUN(9,4,"unsupported-mmio-width");
        RUN(10,0,"unsupported-irq-register-write");
        RUN(11,0,"unsupported-irq-register-read");RUN(11,4,"unsupported-irq-register-read");
        RUN(12,0,"unsupported-ipc-irq-transition");RUN(12,1,"unsupported-ipc-irq-transition");RUN(12,0xc0000000,"unsupported-ipc-irq-transition");
        RUN(13,1,"unsupported-ipc-irq-transition");RUN(14,0,"unsupported-ipc-irq-transition");
        for(unsigned p=0;p<16;++p)RUN(15,1u<<p,"unsupported-mi-irq-enable");
        for(unsigned i=0;i<5;++i)if(widths[i]!=2){RUN(16,widths[i],"unsupported-mmio-width");RUN(17,widths[i],"unsupported-mmio-width");}
        RUN(16,4,"unsupported-mmio-width");RUN(17,4,"unsupported-mmio-width");
        RUN(18,0,"unsupported-irq-register-write");RUN(20,0,"unimplemented-external-pointer");
        RUN(21,0,"mmio-trace-capacity");RUN(22,0,"mmio-trace-capacity");RUN(24,0,"write-to-translated-code");RUN(25,0,"unsupported-mi-irq-enable");
        one(23,0,a,MGX_BOOT_WII_CPU,"unimplemented-memory-write");one(23,0,a,MGX_BOOT_STRICT,"unimplemented-memory-write");
    }
    unsigned unknown[]={0xcc003008,0xcd000038,0xcc00500a,0x8c003004,0xec003004,0x4c003004,0xcc00401e,0x0d800034};
    for(unsigned i=0;i<sizeof(unknown)/sizeof(unknown[0]);++i)one(19,unknown[i],0,MGX_BOOT_WII_IRQ,"unimplemented-memory-write");
    printf("PASS: %u synthetic boot-IRQ cases; %u checks\n",cases,checks);return 0;
}
