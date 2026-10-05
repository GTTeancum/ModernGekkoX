/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,ch,alias,width,continued;
static uint64_t value;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"EXI case%u mode%u ch%u width%u line%d: %s\n",cases,mode,ch,width,__LINE__,#x);exit(1);}}while(0)
static uint32_t addr(void){return 0x0d006800u+20u*ch+(alias?0xc0000000u:0);}
static uint32_t initial(unsigned c){return c<2?0x800u+(c==1?0x80u:0u):0;}
static uint32_t controls(unsigned c){return (c<2?0x405u:5u)|0xf0u|(c==0?0x2000u:0u);}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;uint32_t a=addr();
    c->reserve_valid=1;c->reserve_addr=0x80002000;
    switch(mode){
    case 0:{
        CHECK(mem_read32(c,a)==initial(ch));mem_write32(c,a,(uint32_t)value);
        uint32_t ack=ch<2?0x80au:0xau;
        uint32_t expected=(initial(ch)&~(controls(ch)|((uint32_t)value&ack)))|((uint32_t)value&controls(ch));
        CHECK(mem_read32(c,a)==expected);CHECK(r->exi.status[ch]==expected);
        CHECK(r->irq.event_count==3 && r->irq.reads==2 && r->irq.writes==1);
        CHECK(r->exi.reads[ch]==2 && r->exi.writes[ch]==1);
        uint32_t source=((expected&0xc00u)==0xc00u)?0x10u:0u;
        CHECK(r->irq.pi_cause==(0x10100u|source));CHECK(!r->irq.pi_pending);
        CHECK(r->irq.events[1].value==(uint32_t)value);return 0;
    }
    case 1:c->external_write(c,a,value,width);break;
    case 2:c->external_read(c,a,width);break;
    case 3:c->external_write(c,a+1,value,width);break;
    case 4:c->external_read(c,a+1,width);break;
    case 5:r->exi.status[ch]=(uint32_t)value;c->external_read(c,a,width);break;
    case 6:r->exi.control[ch]=(uint32_t)value;c->external_read(c,a+12,width);break;
    case 7:r->irq.pi_cause|=0x10;c->external_write(c,a,value,width);break;
    case 8: /* Genuine latched EXTINT becomes globally enabled: commit then stop. */
        mem_write32(c,0xcc003004,0x10);mem_write32(c,a,0x400);break;
    case 9:
        CHECK(mem_read32(c,a+12)==0);mem_write32(c,a+12,0);CHECK(mem_read32(c,a+12)==0);return 0;
    case 10:c->external_write(c,a+12,value,width);break;
    case 11:c->external_read(c,a+12,width);break;
    case 12:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,a);
        mem_write32(c,a,1);break;
    case 13:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,a);
        (void)mem_read32(c,a+12);break;
    case 14:c->external_pointer(c,a,width);break;
    case 15:c->external_read(c,(uint32_t)value,width);break;
    case 16:c->external_write(c,(uint32_t)value,0,width);break;
    case 17:mem_write32(c,0x80001004,1);break;
    }
    ++continued;mem_write32(c,0x80002000,1);return 0;
}
static void one(unsigned m,unsigned channel,unsigned al,uint64_t v,unsigned w,mgx_boot_profile profile,const char *why){
    ++cases;mode=m;ch=channel;alias=al;value=v;width=w;continued=0;
    CPUState *c=calloc(1,sizeof(*c));mgx_execution *r=calloc(1,sizeof(*r));CHECK(c && r);
    c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram);
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan p={0};p.entry=0x80001000;p.count=1;
    p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_exec_run_profile(r,c,&memory,&p,dispatch,2,profile);
    if(strcmp(r->stop.reason,why))fprintf(stderr,"expected %s got %s\n",why,r->stop.reason);
    CHECK(!strcmp(r->stop.reason,why));CHECK(!continued);CHECK(!c->exception);CHECK(c->reserve_valid);
    CHECK(!c->ram[0x2003] && !c->ram[0x1007]);CHECK(r->stop.pc==0x80001000);
    if(mode==8){CHECK(r->irq.event_count==2 && r->irq.pi_pending==0x10);CHECK((r->exi.status[ch]&0xc00)==0xc00);}
    else if(mode==12 || mode==13){CHECK(r->irq.event_count==64 && r->irq.reads==64);CHECK(r->exi.status[ch]==initial(ch));}
    else if(mode==9){CHECK(r->irq.event_count==3 && !r->exi.control[ch]);}
    else if(mode!=0){
        CHECK(!r->irq.event_count);
        for(unsigned i=0;i<3;++i){
            uint32_t expected=profile==MGX_BOOT_WII_EXI?initial(i):0;
            if(mode==5 && i==ch)expected=(uint32_t)value;
            CHECK(r->exi.status[i]==expected);
            CHECK(r->exi.control[i]==((mode==6 && i==ch)?(uint32_t)value:0));
        }
    }
    for(unsigned i=0;i<r->irq.event_count;++i)CHECK(r->irq.events[i].pc==0x80001000);
    free(c->ram);free(c);free(r);
}
#define RUN(m,v,w,s) one(m,c,a,v,w,MGX_BOOT_WII_EXI,s)
int main(void){
    for(unsigned a=0;a<2;++a)for(unsigned c=0;c<3;++c){
        /* Independent masks, W1C, clocks, absent-device CS and ROMDIS. */
        for(unsigned masks=0;masks<(c<2?8u:4u);++masks)
        for(unsigned ack=0;ack<(c<2?8u:4u);++ack)
        for(unsigned config=0;config<4;++config){
            uint32_t v=(masks&1)|((masks&2)<<1)|((masks&4)<<8);
            v|=(ack&1)<<1;v|=(ack&2)<<2;v|=(ack&4)<<9;
            if(config&1)v|=0x80;if(config&2)v|=0x70;if(c==0 && config==3)v|=0x2000;
            RUN(0,v,4,"untranslated-address");
        }
        uint32_t legal=controls(c)|(c<2?0x80au:0xau);
        for(unsigned b=0;b<32;++b)if(!((1u<<b)&legal))RUN(1,1u<<b,4,"unsupported-exi-control-transition");
        RUN(1,UINT64_C(0x100000000),4,"unsupported-exi-control-transition");
        unsigned ws[]={0,1,2,3,8};for(unsigned i=0;i<5;++i){RUN(1,0,ws[i],"unsupported-mmio-width");RUN(2,0,ws[i],"unsupported-mmio-width");RUN(10,0,ws[i],"unsupported-mmio-width");RUN(11,0,ws[i],"unsupported-mmio-width");}
        RUN(3,0,4,"unsupported-mmio-width");RUN(4,0,4,"unsupported-mmio-width");
        uint32_t bad[]={2,8,0x100,0x200,0x1000,0x80000000};
        for(unsigned i=0;i<6;++i)RUN(5,bad[i],4,"unsupported-active-exi-state");
        RUN(6,1,4,"unsupported-active-exi-state");RUN(7,0,4,"inconsistent-exi-interrupt-state");
        if(c<2)RUN(8,0,4,"unsupported-pending-pi-interrupt");
        RUN(9,0,4,"untranslated-address");RUN(10,1,4,"unsupported-exi-transfer");RUN(10,UINT64_C(0x100000000),4,"unsupported-exi-transfer");
        RUN(12,0,4,"mmio-trace-capacity");RUN(13,0,4,"mmio-trace-capacity");RUN(14,0,4,"unimplemented-external-pointer");RUN(17,0,4,"write-to-translated-code");
        for(unsigned p=0;p<4;++p){one(1,c,a,0,4,(mgx_boot_profile)p,"unimplemented-memory-write");one(2,c,a,0,4,(mgx_boot_profile)p,"unimplemented-memory-read");}
    }
    uint32_t other[]={0x0d006804,0xcd006808,0xcd006810,0xcd00683c,0xcc006800,0x8d006800,0xed006800,0x4d006800};
    for(unsigned i=0;i<8;++i){one(15,0,0,other[i],4,MGX_BOOT_WII_EXI,"unimplemented-memory-read");one(16,0,0,other[i],4,MGX_BOOT_WII_EXI,"unimplemented-memory-write");}
    printf("PASS: %u synthetic boot-exi cases; %u checks\n",cases,checks);return 0;
}
