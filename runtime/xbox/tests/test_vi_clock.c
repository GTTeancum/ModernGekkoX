/* SPDX-License-Identifier: GPL-3.0-or-later
 * Synthetic VI clock-snapshot boundaries only; no title data or PC exceptions. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,width,continued;
static uint32_t address,input,expected_clock;
static mgx_boot_irq saved_irq;
static mgx_boot_vi_clock saved_vi;
static mgx_boot_exi saved_exi;
static mgx_boot_audio saved_audio;
static mgx_boot_serial saved_serial;
static uint8_t saved_ram[0x4000];
static int captured;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"VI case %u mode %u address %08x width %u input %08x line %d: %s\n",cases,mode,address,width,input,__LINE__,#x);exit(1);} } while(0)
static void capture(CPUState *c){
    mgx_execution *r=c->external_user_data;
    saved_irq=r->irq;saved_vi=r->vi_clock;saved_exi=r->exi;
    saved_audio=r->audio;saved_serial=r->serial;
    memcpy(saved_ram,c->ram,sizeof(saved_ram));captured=1;
}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;
    c->reserve_valid=1;c->reserve_addr=0x80003000;
    c->gpr[8]=0x13579bdf;c->cr=0xabcdef01;c->xer=0xe0000000;
    switch(mode){
    case 0: /* Real guest halfword helper, independent original addresses/PCs. */
        CHECK(r->vi_clock.clock==expected_clock && !r->vi_clock.reads);
        for(unsigned i=0;i<3;++i){
            c->pc=0x80001000+4*i;
            CHECK(mem_read16(c,address)==expected_clock);
            CHECK(r->vi_clock.reads==i+1 && r->irq.event_count==i+1);
            const mgx_mmio_event *e=&r->irq.events[i];
            CHECK(e->pc==c->pc && e->address==address && e->value==expected_clock);
            CHECK(e->width==2 && !e->is_write);
        }
        CHECK(r->irq.reads==3 && !r->irq.writes && r->irq.pi_cause==0x10100 && !r->irq.pi_pending);
        return 0;
    case 1:capture(c);c->external_read(c,address,width);break;
    case 2:capture(c);c->external_write(c,address,input,width);break;
    case 3:r->vi_clock.clock=input;capture(c);(void)mem_read16(c,address);break;
    case 4: /* Fill through real accesses; last slot succeeds and next fails atomically. */
        for(unsigned i=0;i<MGX_SERIAL_MMIO_CAPACITY;++i){
            c->pc=0x80001000+(i%64)*4;
            CHECK(mem_read16(c,address)==expected_clock);
        }
        CHECK(r->vi_clock.reads==MGX_SERIAL_MMIO_CAPACITY);
        CHECK(r->irq.reads==MGX_SERIAL_MMIO_CAPACITY && !r->irq.writes);
        for(unsigned i=0;i<MGX_SERIAL_MMIO_CAPACITY;++i){
            const mgx_mmio_event *e=&r->irq.events[i];
            CHECK(e->pc==0x80001000+(i%64)*4 && e->address==address && e->value==expected_clock && e->width==2 && !e->is_write);
        }
        capture(c);(void)mem_read16(c,address);break;
    case 5: /* Other accepted devices consume the same bounded event space. */
        for(unsigned i=0;i<MGX_SERIAL_MMIO_CAPACITY;++i)(void)mem_read32(c,0xcc003000);
        capture(c);(void)mem_read16(c,address);break;
    case 6:capture(c);c->external_pointer(c,address,width);break;
    case 7: /* PI source remains meaningful; no synthetic delivery. */
        mem_write32(c,0xcc003004,0x100);break;
    case 8:capture(c);mem_write32(c,0x80001004,input);break;
    case 9:capture(c);(void)mem_read8(c,address);break;
    case 10:capture(c);(void)mem_read32(c,address);break;
    case 11:capture(c);(void)mem_read64(c,address);break;
    case 12:capture(c);mem_write16(c,address,(uint16_t)input);break;
    }
    ++continued;mem_write32(c,0x80003000,0xbad);return 0;
}
static void one(unsigned m,uint32_t a,uint32_t v,unsigned w,mgx_boot_profile profile,const char *reason){
    ++cases;mode=m;address=a;input=v;width=w;continued=0;captured=0;
    expected_clock=profile==MGX_BOOT_WII_VI_CLOCK_27MHZ?MGX_VI_CLOCK_27MHZ:MGX_VI_CLOCK_REFERENCE_NTSC;
    CPUState *c=calloc(1,sizeof(*c));mgx_execution *r=calloc(1,sizeof(*r));CHECK(c&&r);
    c->ram_size=sizeof(saved_ram);c->ram=calloc(1,c->ram_size);CHECK(c->ram);
    mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan plan={0};plan.entry=0x80001000;plan.count=1;
    plan.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_exec_run_profile(r,c,&memory,&plan,dispatch,2,profile);
    if(strcmp(r->stop.reason,reason))fprintf(stderr,"expected %s; got %s\n",reason,r->stop.reason);
    CHECK(!strcmp(r->stop.reason,reason));CHECK(!continued && !c->exception);
    CHECK(c->reserve_valid && c->reserve_addr==0x80003000);
    CHECK(c->gpr[8]==0x13579bdf && c->cr==0xabcdef01 && c->xer==0xe0000000);
    CHECK(c->ram[0x3003]==0 && c->ram[0x1007]==0);
    if(captured){
        CHECK(!memcmp(&r->irq,&saved_irq,sizeof(saved_irq)));
        CHECK(!memcmp(&r->vi_clock,&saved_vi,sizeof(saved_vi)));
        CHECK(!memcmp(&r->exi,&saved_exi,sizeof(saved_exi)));
        CHECK(!memcmp(&r->audio,&saved_audio,sizeof(saved_audio)));
        CHECK(!memcmp(&r->serial,&saved_serial,sizeof(saved_serial)));
        CHECK(!memcmp(c->ram,saved_ram,sizeof(saved_ram)));
        CHECK(r->stop.address==(m==8?0x80001004:address));
        CHECK(r->stop.width==(m==8?4:(m==3 || m==4 || m==5 || m==12)?2:m==9?1:m==10?4:m==11?8:width));
        CHECK(r->stop.value==((m==2 || m==8)?input:m==12?(uint16_t)input:0));
    }
    if(m==7)CHECK(r->irq.pi_mask==0x100 && r->irq.pi_pending==0x100 && r->irq.writes==1 && !r->vi_clock.reads);
    if(m!=0 && m!=4)CHECK(!r->vi_clock.reads);
    free(c->ram);free(c);free(r);
}
int main(void){
    mgx_boot_profile profiles[]={MGX_BOOT_WII_VI_CLOCK_27MHZ,MGX_BOOT_WII_VI_CLOCK_NTSC,MGX_BOOT_WII_SI_POLL_DORMANT};
    for(unsigned p=0;p<3;++p)for(unsigned al=0;al<2;++al){
        mgx_boot_profile profile=profiles[p];uint32_t a=0x0c00206c+(al?0xc0000000u:0);
        one(0,a,0,2,profile,"untranslated-address");
        /* All callback widths, including malformed/non-scalar widths, and all writes. */
        for(unsigned w=0;w<256;++w){
            if(w!=2)one(1,a,0,w,profile,"unsupported-mmio-width");
            one(1,a+1,0,w,profile,"unsupported-mmio-width");
            one(2,a,0xdeadbeef,w,profile,"unsupported-vi-clock-write");
            one(2,a+1,0xdeadbeef,w,profile,"unsupported-vi-clock-write");
        }
        /* Both valid selections and every bit outside the admitted snapshot domain. */
        for(unsigned bit=1;bit<32;++bit){
            one(3,a,1u<<bit,2,profile,"invalid-vi-clock-snapshot");
            one(3,a,(1u<<bit)|1u,2,profile,"invalid-vi-clock-snapshot");
        }
        one(3,a,UINT32_MAX,2,profile,"invalid-vi-clock-snapshot");
        one(4,a,0,2,profile,"mmio-trace-capacity");
        one(5,a,0,2,profile,"mmio-trace-capacity");
        one(6,a,0,2,profile,"unimplemented-external-pointer");
        one(7,a,0,2,profile,"unsupported-pending-pi-interrupt");
        one(8,a,0x12345678,4,profile,"write-to-translated-code");
        one(9,a,0,1,profile,"unsupported-mmio-width");
        one(10,a,0,4,profile,"unsupported-mmio-width");
        one(11,a,0,8,profile,"unsupported-mmio-width");
        one(12,a,0,2,profile,"unsupported-vi-clock-write");
        one(12,a,1,2,profile,"unsupported-vi-clock-write");
        one(12,a,0xffff,2,profile,"unsupported-vi-clock-write");
        /* Neighbor registers (including DTV), no implicit paired/offset behavior. */
        for(unsigned off=0;off<0x100;++off)if(off!=0x6c && off!=0x6d){
            uint32_t other=(a&~0xffu)+off;
            one(1,other,0,2,profile,"unimplemented-memory-read");
            one(2,other,0x1234,2,profile,"unimplemented-memory-write");
        }
        for(unsigned old=MGX_BOOT_STRICT;old<=MGX_BOOT_WII_SERIAL;++old){
            one(1,a,0,2,(mgx_boot_profile)old,"unimplemented-memory-read");
            one(2,a,1,2,(mgx_boot_profile)old,"unimplemented-memory-write");
        }
    }
    uint32_t other[]={0x8c00206c,0xec00206c,0x4c00206c,0x0d00206c,0xcd00206c,0x8d00206c,0xcc00216c,0x0c00106c,0x0d006430,0xcd006430};
    for(unsigned p=0;p<3;++p)for(unsigned i=0;i<sizeof(other)/sizeof(*other);++i){
        one(1,other[i],0,2,profiles[p],p==2 && i>=8?"unsupported-mmio-width":"unimplemented-memory-read");
        one(2,other[i],0x1234,4,profiles[p],p==2 && i>=8?"unsupported-si-poll-configuration":"unimplemented-memory-write");
    }
    printf("PASS: %u synthetic VI-clock cases; %u checks\n",cases,checks);return 0;
}
