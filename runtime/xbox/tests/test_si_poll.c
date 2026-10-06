/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic dormant SI_POLL configuration only; no title addresses or hooks. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,width,continued,journal_writes;
static uint32_t address;
static uint64_t input;
static mgx_boot_irq saved_irq;
static mgx_boot_si_poll saved_si;
static mgx_boot_vi_clock saved_vi;
static mgx_boot_exi saved_exi;
static mgx_boot_audio saved_audio;
static mgx_boot_serial saved_serial;
static uint8_t saved_ram[0x4000];
static int captured,prime_history;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"SI case %u mode %u address %08x width %u input %016llx line %d: %s\n",cases,mode,address,width,(unsigned long long)input,__LINE__,#x);exit(1);} } while(0)
static void capture(CPUState *c){
    mgx_execution *r=c->external_user_data;
    saved_irq=r->irq;saved_si=r->si_poll;saved_vi=r->vi_clock;
    saved_exi=r->exi;saved_audio=r->audio;saved_serial=r->serial;
    memcpy(saved_ram,c->ram,sizeof(saved_ram));captured=1;
}
static void event(mgx_execution *r,unsigned n,uint32_t pc,uint32_t a,uint32_t v,int wr){
    CHECK(n<r->irq.event_count);const mgx_mmio_event *e=&r->irq.events[n];
    CHECK(e->pc==pc && e->address==a && e->value==v && e->width==4 && e->is_write==(unsigned)wr);
}
static void journal(uint32_t a,uint32_t n,void *u){(void)a;(void)n;(void)u;++journal_writes;}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;
    c->reserve_valid=1;c->reserve_addr=0x80003000;
    c->gpr[8]=0x13579bdf;c->cr=0xabcdef01;c->xer=0xe0000000;
    ppc_set_mem_write_journal(journal,NULL);
    if(prime_history){
        uint32_t a=0x0d006430u|(address&0xc0000000u);
        mem_write32(c,a,MGX_SI_POLL_REFERENCE|0xa500);
        CHECK(mem_read32(c,a^0xc0000000u)==(MGX_SI_POLL_REFERENCE|0xa500));
        CHECK(r->si_poll.reads==1 && r->si_poll.writes==1 && r->irq.event_count==2);
        event(r,0,c->pc,a,MGX_SI_POLL_REFERENCE|0xa500,1);
        event(r,1,c->pc,a^0xc0000000u,MGX_SI_POLL_REFERENCE|0xa500,0);
    }
    switch(mode){
    case 0:{ /* All Y values, idempotence and coherent explicit aliases. */
        CHECK(r->si_poll.poll==MGX_SI_POLL_REFERENCE && !r->si_poll.reads && !r->si_poll.writes);
        CHECK(r->vi_clock.clock==MGX_VI_CLOCK_REFERENCE_NTSC);
        CHECK(mem_read32(c,address)==MGX_SI_POLL_REFERENCE);
        event(r,0,c->pc,address,MGX_SI_POLL_REFERENCE,0);
        uint32_t alias=address^0xc0000000u;
        for(unsigned y=0;y<256;++y){
            uint32_t v=MGX_SI_POLL_REFERENCE|(y<<8);unsigned n=1+y*3;
            c->pc=0x80001000+(y%64)*4;
            mem_write32(c,address,v);event(r,n,c->pc,address,v,1);
            mem_write32(c,alias,v);event(r,n+1,c->pc,alias,v,1);
            CHECK(mem_read32(c,alias)==v);event(r,n+2,c->pc,alias,v,0);
            CHECK(r->si_poll.poll==v && r->si_poll.writes==2*(y+1) && r->si_poll.reads==y+2);
            CHECK(r->irq.event_count==n+3 && !journal_writes);
        }
        CHECK(r->irq.reads==257 && r->irq.writes==512 && r->irq.pi_cause==0x10100 && !r->irq.pi_pending);
        CHECK(!memcmp(c->ram,saved_ram,sizeof(saved_ram)));
        return 0;}
    case 1:capture(c);c->external_read(c,address,width);break;
    case 2:capture(c);c->external_write(c,address,input,width);break;
    case 3:r->si_poll.poll=(uint32_t)input;capture(c);(void)mem_read32(c,address);break;
    case 4:r->si_poll.poll=(uint32_t)input;capture(c);mem_write32(c,address,MGX_SI_POLL_REFERENCE);break;
    case 5:r->irq.pi_cause|=8;capture(c);(void)mem_read32(c,address);break;
    case 6:r->irq.pi_cause|=8;capture(c);mem_write32(c,address,MGX_SI_POLL_REFERENCE);break;
    case 7:r->irq.pi_pending|=8;capture(c);(void)mem_read32(c,address);break;
    case 8:r->irq.pi_pending|=8;capture(c);mem_write32(c,address,MGX_SI_POLL_REFERENCE);break;
    case 9:case 10: /* Actual successful SI operations fill every log slot. */
        for(unsigned i=0;i<MGX_SERIAL_MMIO_CAPACITY;++i){
            c->pc=0x80001000+(i%64)*4;
            if(i&1)mem_write32(c,address,MGX_SI_POLL_REFERENCE|((i&255)<<8));
            else CHECK(mem_read32(c,address)==r->si_poll.poll);
            event(r,i,c->pc,address,r->si_poll.poll,i&1);
        }
        CHECK(r->si_poll.reads==2048 && r->si_poll.writes==2048);
        capture(c);
        if(mode==9)(void)mem_read32(c,address);else mem_write32(c,address,MGX_SI_POLL_REFERENCE);
        break;
    case 11:case 12: /* Shared log: another device can exhaust capacity. */
        for(unsigned i=0;i<MGX_SERIAL_MMIO_CAPACITY;++i)(void)mem_read32(c,0xcc003000);
        capture(c);
        if(mode==11)(void)mem_read32(c,address);else mem_write32(c,address,MGX_SI_POLL_REFERENCE);
        break;
    case 13:capture(c);c->external_pointer(c,address,width);break;
    case 14: /* Committed PI mask still stops, even if MSR.EE is clear. */
        c->msr&=~0x8000u;r->irq.pi_cause|=8;mem_write32(c,0xcc003004,8);break;
    case 15:capture(c);mem_write32(c,0x80001004,(uint32_t)input);break;
    case 16:capture(c);(void)mem_read8(c,address);break;
    case 17:capture(c);(void)mem_read16(c,address);break;
    case 18:capture(c);(void)mem_read64(c,address);break;
    case 19:capture(c);mem_write8(c,address,(uint8_t)input);break;
    case 20:capture(c);mem_write16(c,address,(uint16_t)input);break;
    case 21:capture(c);mem_write64(c,address,input);break;
    case 22:CHECK(mem_read16(c,0xcc00206c)==1 && r->vi_clock.reads==1);return 0;
    }
    ++continued;mem_write32(c,0x80003000,0xbad);return 0;
}
static void one(unsigned m,uint32_t a,uint64_t v,unsigned w,mgx_boot_profile profile,const char *reason){
    ++cases;mode=m;address=a;input=v;width=w;continued=0;captured=0;journal_writes=0;
    memset(saved_ram,0,sizeof(saved_ram));
    CPUState *c=calloc(1,sizeof(*c));mgx_execution *r=calloc(1,sizeof(*r));CHECK(c&&r);
    c->ram_size=sizeof(saved_ram);c->ram=calloc(1,c->ram_size);CHECK(c->ram);
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.entry=0x80001000;plan.count=1;
    plan.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_exec_run_profile(r,c,&memory,&plan,dispatch,2,profile);
    if(strcmp(r->stop.reason,reason))fprintf(stderr,"expected %s; got %s\n",reason,r->stop.reason);
    CHECK(!strcmp(r->stop.reason,reason));CHECK(!continued && !c->exception && !journal_writes);
    CHECK(c->reserve_valid && c->reserve_addr==0x80003000);
    CHECK(c->gpr[8]==0x13579bdf && c->cr==0xabcdef01 && c->xer==0xe0000000);
    CHECK(!memcmp(c->ram,saved_ram,sizeof(saved_ram)));
    CHECK(!r->identical_code_writes && !r->template_writes);
    if(captured){
        CHECK(!memcmp(&r->irq,&saved_irq,sizeof(saved_irq)));
        CHECK(!memcmp(&r->si_poll,&saved_si,sizeof(saved_si)));
        CHECK(!memcmp(&r->vi_clock,&saved_vi,sizeof(saved_vi)));
        CHECK(!memcmp(&r->exi,&saved_exi,sizeof(saved_exi)));
        CHECK(!memcmp(&r->audio,&saved_audio,sizeof(saved_audio)));
        CHECK(!memcmp(&r->serial,&saved_serial,sizeof(saved_serial)));
        CHECK(r->stop.address==(m==15?0x80001004:address));
        unsigned want_width=(m==1 || m==2 || m==13)?width:(m==16 || m==19)?1:(m==17 || m==20)?2:(m==18 || m==21)?8:4;
        uint64_t want_value=m==2 || m==15 || m==21?input:m==19?(uint8_t)input:m==20?(uint16_t)input:(m==4 || m==6 || m==8 || m==10 || m==12)?MGX_SI_POLL_REFERENCE:0;
        CHECK(r->stop.width==want_width && r->stop.value==want_value);
    }
    if(m==14)CHECK(r->irq.pi_mask==8 && r->irq.pi_pending==8 && r->irq.writes==1 && !r->si_poll.reads && !r->si_poll.writes);
    free(c->ram);free(c);free(r);
}
#define RUN(m,a,v,w,why) one(m,a,v,w,MGX_BOOT_WII_SI_POLL_DORMANT,why)
int main(void){
    for(unsigned al=0;al<2;++al){
        uint32_t a=0x0d006430u+(al?0xc0000000u:0);
        RUN(0,a,0,4,"untranslated-address");RUN(22,a,0,4,"untranslated-address");
        for(unsigned w=0;w<256;++w)for(unsigned off=0;off<4;++off){
            if(w==4 && !off)continue;
            RUN(1,a+off,0,w,"unsupported-mmio-width");
            RUN(2,a+off,MGX_SI_POLL_REFERENCE,w,"unsupported-mmio-width");
        }
        /* Every forbidden bit, all X choices, malformed current state and high bits. */
        for(unsigned bit=0;bit<64;++bit){
            if(bit>=8 && bit<16)continue;
            uint64_t bad=(uint64_t)MGX_SI_POLL_REFERENCE^(UINT64_C(1)<<bit);
            RUN(2,a,bad,4,"unsupported-si-poll-configuration");
            if(bit<32){RUN(3,a,bad,4,"unsupported-active-si-poll-state");RUN(4,a,bad,4,"unsupported-active-si-poll-state");}
        }
        for(unsigned x=0;x<1024;++x)if(x!=492){
            uint32_t bad=(x<<16)|0xff00;
            RUN(2,a,bad,4,"unsupported-si-poll-configuration");
            RUN(3,a,bad,4,"unsupported-active-si-poll-state");RUN(4,a,bad,4,"unsupported-active-si-poll-state");
        }
        for(unsigned m=5;m<=8;++m)RUN(m,a,0,4,"unsupported-active-si-poll-state");
        for(unsigned m=9;m<=12;++m)RUN(m,a,0,4,"mmio-trace-capacity");
        for(unsigned w=0;w<=8;++w)RUN(13,a,0,w,"unimplemented-external-pointer");
        RUN(14,a,0,4,"unsupported-pending-pi-interrupt");RUN(15,a,0x12345678,4,"write-to-translated-code");
        for(unsigned m=16;m<=21;++m)RUN(m,a,UINT64_MAX,4,"unsupported-mmio-width");
        RUN(2,a,UINT64_MAX,4,"unsupported-si-poll-configuration");
        RUN(2,a,0x00f60200,4,"unsupported-si-poll-configuration");
        for(unsigned off=0;off<256;++off)if(off<0x30 || off>=0x34){
            RUN(1,(a&~0xffu)+off,0,4,"unimplemented-memory-read");
            RUN(2,(a&~0xffu)+off,0,4,"unimplemented-memory-write");
        }
        /* Rejections must preserve populated counters, log and nonzero Y too. */
        prime_history=1;
        RUN(1,a+1,0,4,"unsupported-mmio-width");
        RUN(2,a,MGX_SI_POLL_REFERENCE,2,"unsupported-mmio-width");
        RUN(2,a,MGX_SI_POLL_REFERENCE|0x80,4,"unsupported-si-poll-configuration");
        RUN(3,a,MGX_SI_POLL_REFERENCE|0x1,4,"unsupported-active-si-poll-state");
        RUN(4,a,MGX_SI_POLL_REFERENCE^0x10000,4,"unsupported-active-si-poll-state");
        for(unsigned m=5;m<=8;++m)RUN(m,a,0,4,"unsupported-active-si-poll-state");
        RUN(13,a,0,4,"unimplemented-external-pointer");
        RUN(1,a+4,0,4,"unimplemented-memory-read");
        RUN(2,a+4,0,4,"unimplemented-memory-write");
        prime_history=0;
        for(unsigned old=MGX_BOOT_STRICT;old<=MGX_BOOT_WII_VI_CLOCK_27MHZ;++old){
            one(1,a,0,4,(mgx_boot_profile)old,"unimplemented-memory-read");
            one(2,a,MGX_SI_POLL_REFERENCE,4,(mgx_boot_profile)old,"unimplemented-memory-write");
        }
    }
    uint32_t unsupported[]={0x0c006430,0xcc006430,0x0d806430,0xcd806430,0x8d006430,0x4d006430,0xed006430,0x0d00642f,0xcd00642f};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(*unsupported);++i){
        RUN(1,unsupported[i],0,4,"unimplemented-memory-read");
        RUN(2,unsupported[i],MGX_SI_POLL_REFERENCE,4,"unimplemented-memory-write");
    }
    printf("PASS: %u synthetic SI-poll cases; %u checks\n",cases,checks);return 0;
}
