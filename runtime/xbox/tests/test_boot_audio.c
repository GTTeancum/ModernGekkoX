/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,width,reg,alias,continued;
static uint64_t value;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"case %u mode%u reg%u alias%u width%u value%llx line%d: %s\n",cases,mode,reg,alias,width,(unsigned long long)value,__LINE__,#x);exit(1);}}while(0)
static uint32_t address(void){return (reg==1?0x0c00500au:0x0d006c00u)+(alias?0xc0000000u:0);}
static uint32_t initial(void){return reg==1?MGX_DSP_IDLE_CONTROL:MGX_AI_IDLE_CONTROL;}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;uint32_t a=address();
    c->reserve_valid=1;c->reserve_addr=0x80002000;
    switch(mode){
    case 0:{ /* Actual guest load/store helpers and stored mask readback. */
        CHECK((reg==1?mem_read16(c,a):mem_read32(c,a))==initial());
        if(reg==1)mem_write16(c,a,(uint16_t)value);else mem_write32(c,a,(uint32_t)value);
        uint32_t expected=(uint32_t)value&~(reg==1?0xa8u:8u);
        CHECK((reg==1?mem_read16(c,a):mem_read32(c,a))==expected);
        CHECK(r->irq.event_count==3 && r->irq.reads==2 && r->irq.writes==1);
        CHECK(r->irq.events[1].value==(uint32_t)value && r->irq.events[1].is_write);
        CHECK(r->irq.pi_cause==0x10100 && r->irq.pi_pending==0);
        CHECK(c->reserve_valid);return 0;
    }
    case 1:c->external_write(c,a,value,width);break;
    case 2:c->external_read(c,a,width);break;
    case 3:c->external_read(c,a+1,width);break;
    case 4:c->external_write(c,a+1,value,width);break;
    case 5: /* Invalid/live state must not be silently reset by a mask write. */
        if(reg==1)r->audio.dsp_control=(uint32_t)value;else r->audio.ai_control=(uint32_t)value;
        c->external_write(c,a,initial(),width);break;
    case 6:
        if(reg==1)r->audio.dsp_control=(uint32_t)value;else r->audio.ai_control=(uint32_t)value;
        c->external_read(c,a,width);break;
    case 7:r->irq.pi_cause|=(uint32_t)value;c->external_read(c,a,width);break;
    case 8:r->irq.pi_cause|=(uint32_t)value;c->external_write(c,a,initial(),width);break;
    case 9:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,0xcc003000);
        c->external_write(c,a,value,width);break;
    case 10:
        for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,0xcc003000);
        c->external_read(c,a,width);break;
    case 11:c->external_pointer(c,a,width);break;
    case 12:c->external_read(c,(uint32_t)value,width);break;
    case 13:c->external_write(c,(uint32_t)value,initial(),width);break;
    case 14:mem_write32(c,0xcc003004,0x100);break; /* Old pending IRQ stop retained. */
    case 15:mem_write32(c,0x80001004,0x12345678);break; /* Text guard retained. */
    }
    ++continued;mem_write32(c,0x80002000,1);return 0;
}
static void one(unsigned m,unsigned rg,unsigned al,uint64_t v,unsigned w,mgx_boot_profile p,const char *reason){
    ++cases;mode=m;reg=rg;alias=al;value=v;width=w;continued=0;
    CPUState *c=calloc(1,sizeof(*c));mgx_execution *r=calloc(1,sizeof(*r));CHECK(c && r);
    c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram!=NULL);
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.count=1;plan.entry=0x80001000;
    plan.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_exec_run_profile(r,c,&memory,&plan,dispatch,2,p);
    if(strcmp(r->stop.reason,reason))fprintf(stderr,"expected %s; got %s\n",reason,r->stop.reason);
    CHECK(!strcmp(r->stop.reason,reason));CHECK(!continued);CHECK(!c->exception);CHECK(c->reserve_valid);
    CHECK(c->ram[0x2003]==0 && c->ram[0x1007]==0);
    CHECK(r->stop.pc==0x80001000);
    if(m==0){
        CHECK(r->audio.dsp_reads==(rg==1?2u:0u));CHECK(r->audio.ai_reads==(rg==2?2u:0u));
        CHECK(r->audio.dsp_writes==(rg==1?1u:0u));CHECK(r->audio.ai_writes==(rg==2?1u:0u));
    }else{
        CHECK(!r->audio.dsp_reads && !r->audio.ai_reads && !r->audio.dsp_writes && !r->audio.ai_writes);
        if(m==9 || m==10)CHECK(r->irq.event_count==64 && r->irq.reads==64 && r->irq.writes==0);
        else if(m==14)CHECK(r->irq.event_count==1 && r->irq.pi_mask==0x100 && r->irq.pi_pending==0x100);
        else CHECK(r->irq.event_count==0);
        if(p==MGX_BOOT_WII_AUDIO){
            CHECK(r->audio.dsp_control==((m==5 || m==6)&&rg==1?(uint32_t)v:MGX_DSP_IDLE_CONTROL));
            CHECK(r->audio.ai_control==((m==5 || m==6)&&rg==2?(uint32_t)v:MGX_AI_IDLE_CONTROL));
        }
    }
    for(unsigned i=0;i<r->irq.event_count;++i)CHECK(r->irq.events[i].pc==0x80001000);
    free(c->ram);free(c);free(r);
}
#define RUN(m,v,w,why) one(m,g,a,v,w,MGX_BOOT_WII_AUDIO,why)
int main(void){
    for(unsigned a=0;a<2;++a)for(unsigned g=1;g<=2;++g){
        const unsigned w=g==1?2:4;const uint32_t start=g==1?MGX_DSP_IDLE_CONTROL:MGX_AI_IDLE_CONTROL;
        for(unsigned mask=0;mask<(g==1?8u:4u);++mask)for(unsigned ack=0;ack<(g==1?8u:2u);++ack){
            uint32_t v=start;
            if(g==1){for(unsigned k=0;k<3;++k){if(mask&(1u<<k))v|=0x10u<<(2*k);if(ack&(1u<<k))v|=8u<<(2*k);}}
            else {if(mask&1)v|=4;if(mask&2)v|=16;if(ack)v|=8;}
            RUN(0,v,w,"untranslated-address");
        }
        for(unsigned bit=0;bit<32;++bit){
            if((1u<<bit)&(g==1?0x1f8u:0x1cu))continue;
            RUN(1,start^(1u<<bit),w,g==1?"unsupported-dsp-control-transition":"unsupported-ai-control-transition");
        }
        RUN(1,UINT64_C(0x100000000)|start,w,g==1?"unsupported-dsp-control-transition":"unsupported-ai-control-transition");
        unsigned ws[]={0,1,2,3,4,8};
        for(unsigned i=0;i<6;++i)if(ws[i]!=w){RUN(1,start,ws[i],"unsupported-mmio-width");RUN(2,0,ws[i],"unsupported-mmio-width");}
        RUN(3,0,w,"unsupported-mmio-width");RUN(4,start,w,"unsupported-mmio-width");
        uint32_t bad_dsp[]={0x805,0x806,0x800,4,0xc04,0xa04,0x80c,0x824,0x884,0x1804};
        uint32_t bad_ai[]={0x43,0x40,2,0x62,0x4a,0x80000042};
        uint32_t *bad=g==1?bad_dsp:bad_ai;unsigned n=g==1?10:6;
        for(unsigned i=0;i<n;++i){RUN(5,bad[i],w,"unsupported-active-audio-state");RUN(6,bad[i],w,"unsupported-active-audio-state");}
        RUN(7,0x20,w,"unsupported-active-audio-state");RUN(7,0x40,w,"unsupported-active-audio-state");
        RUN(8,0x20,w,"unsupported-active-audio-state");RUN(8,0x40,w,"unsupported-active-audio-state");
        RUN(9,start|(g==1?0x150u:0x14u),w,"mmio-trace-capacity");RUN(10,0,w,"mmio-trace-capacity");
        RUN(11,0,w,"unimplemented-external-pointer");RUN(14,0,w,"unsupported-pending-pi-interrupt");RUN(15,0,w,"write-to-translated-code");
        for(unsigned p=0;p<3;++p){one(1,g,a,start,w,(mgx_boot_profile)p,"unimplemented-memory-write");one(2,g,a,0,w,(mgx_boot_profile)p,"unimplemented-memory-read");}
    }
    uint32_t other[]={0x0c005008,0xcc00500c,0xcc005000,0xcc005036,0xcd006c04,0x0d006c08,0xcd006c0c,0x8c00500a,0xec00500a,0x4c00500a,0xcc006c00,0x8d006c00,0xed006c00,0x4d006c00};
    for(unsigned i=0;i<sizeof(other)/sizeof(other[0]);++i){one(12,1,0,other[i],2,MGX_BOOT_WII_AUDIO,"unimplemented-memory-read");one(13,1,0,other[i],2,MGX_BOOT_WII_AUDIO,"unimplemented-memory-write");}
    printf("PASS: %u synthetic boot-audio cases; %u checks\n",cases,checks);return 0;
}
