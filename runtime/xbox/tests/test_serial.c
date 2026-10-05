/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,alias,length,split,continued;
static uint32_t input;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"serial case %u mode %u line %d: %s\n",cases,mode,__LINE__,#x);exit(1);} } while(0)
static uint32_t base(void){return alias?0xcd006800u:0x0d006800u;}
static void shift(CPUState *c,uint32_t word,unsigned n,unsigned write){
    mem_write32(c,base()+16,word);
    mem_write32(c,base()+12,((n-1u)<<4)|(write<<2)|1u);
}
static void select_new(CPUState *c){mem_write32(c,base(),0);mem_write32(c,base(),0x140);}
static void command(CPUState *c,uint32_t word){select_new(c);shift(c,word,4,1);}
static int dispatch(CPUState *c,uint32_t pc){
    (void)pc;mgx_execution *r=c->external_user_data;
    c->reserve_valid=1;c->reserve_addr=0x80003000;
    switch(mode){
    case 0: /* Real byte shifting, both init markers, no invented readback. */
        command(c,0xb0000000);shift(c,0xf2000000,4,1);
        command(c,0xb0000000);shift(c,0xf3000000,4,1);
        CHECK(r->serial.commands==2 && r->serial.config_bytes==8);
        CHECK(r->serial.config_f2==1 && r->serial.config_f3==1);
        CHECK(!r->serial.output_bytes && r->exi.transfers[0]==4);
        CHECK(r->exi.status[0]==0x948 && r->exi.control[0]==0x34);
        return 0;
    case 1: /* Split command, including a final burst containing payload. */
        select_new(c);shift(c,0xb0000100,split,1);
        CHECK(r->serial.command_bytes==split && !r->serial.commands);
        shift(c,0xb0000100u<<(split*8),4-split,1);
        CHECK(r->serial.command==0xb0000100 && r->serial.commands==1);
        shift(c,input,length,1);
        for(unsigned i=0,n=0;i<length;++i){uint8_t b=(uint8_t)(input>>(24-8*i));if(b)CHECK(r->serial.output[n++]==b);}
        return 0;
    case 2: /* FIFO occupancy reflects a synchronous, bounded capture sink. */
        command(c,0xb0000100);shift(c,0x41420043,4,1);
        CHECK(r->serial.output_bytes==3 && !memcmp(r->serial.output,"ABC",3));
        command(c,0x30000100);shift(c,0xffffffff,length,0);
        CHECK(!mem_read32(c,base()+16) && r->serial.queue_reads==length);
        CHECK(r->serial.output_bytes==3);return 0;
    case 3: /* Invalid command must not commit the final command burst. */
        select_new(c);shift(c,input,4,1);break;
    case 4: /* Read before a complete header is unsupported. */
        select_new(c);shift(c,0xb0000000,split,1);shift(c,0,1,0);break;
    case 5: /* Command direction is not silently ignored. */
        command(c,input);shift(c,0x41424344,4,input==0x30000100?1:0);break;
    case 6: /* Atomic per-transfer overflow: neither first byte nor latch changes. */
        command(c,0xb0000100);r->serial.output_bytes=MGX_SERIAL_CAPACITY-1;
        r->serial.output[MGX_SERIAL_CAPACITY-1]=0xa5;
        shift(c,0x41420000,2,1);break;
    case 7:
        command(c,0x30000100);r->serial.output_bytes=MGX_SERIAL_CAPACITY-15;
        shift(c,0,1,0);break;
    case 8: /* Rewriting the same select must not reset an in-flight header. */
        select_new(c);shift(c,0xb0000000,2,1);mem_write32(c,base(),0x140);
        CHECK(r->serial.command_bytes==2);shift(c,0x01000000,2,1);
        CHECK(r->serial.command==0xb0000100);shift(c,0x51000000,1,1);
        CHECK(r->serial.output_bytes==1 && r->serial.output[0]=='Q');return 0;
    case 9: /* Reselect clears command state, not previously captured output. */
        command(c,0xb0000100);shift(c,0x58000000,1,1);select_new(c);
        CHECK(!r->serial.command_bytes && !r->serial.command && r->serial.output_bytes==1);return 0;
    case 10: /* Source assertion commits supported transfer before pending stop. */
        mem_write32(c,0xcc003004,0x10);mem_write32(c,base(),0x144);
        shift(c,0xb0000000,4,1);break;
    case 11:
        command(c,0xb0000100);r->irq.event_count=MGX_SERIAL_MMIO_CAPACITY;
        mem_write32(c,base()+12,0x35);break;
    case 12:r->serial.command_bytes=5;mem_read32(c,base());break;
    case 13:r->serial.output_bytes=MGX_SERIAL_CAPACITY+1;mem_read32(c,base());break;
    case 14:mem_write32(c,base(),input);break;
    case 15: /* Command can straddle transfer boundaries atomically. */
        select_new(c);shift(c,0xb0000100,3,1);shift(c,0x00414243,4,1);
        CHECK(r->serial.commands==1 && r->serial.output_bytes==3);
        CHECK(!memcmp(r->serial.output,"ABC",3));return 0;
    case 16: /* Generic immediate/DMA violations stay fail-closed. */
        command(c,0xb0000100);mem_write32(c,base()+12,input);break;
    case 17:command(c,0xb0000100);c->external_pointer(c,base()+16,4);break;
    case 18: /* All earlier profiles retain refusal at the IPL select. */
        mem_write32(c,base(),0x140);break;
    }
    ++continued;mem_write32(c,0x80003000,0xbad);return 0;
}
static void one(unsigned m,uint32_t v,mgx_boot_profile profile,const char *why){
    ++cases;mode=m;input=v;continued=0;
    CPUState *c=calloc(1,sizeof(*c));mgx_execution *r=calloc(1,sizeof(*r));CHECK(c&&r);
    c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram);
    mgx_memory mem={c->ram,c->ram_size,NULL,0};mgx_dol_plan p={0};p.entry=0x80001000;p.count=1;
    p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
    mgx_exec_run_profile(r,c,&mem,&p,dispatch,2,profile);
    if(strcmp(r->stop.reason,why))fprintf(stderr,"expected %s got %s\n",why,r->stop.reason);
    CHECK(!strcmp(r->stop.reason,why));CHECK(!continued && !c->exception && c->reserve_valid);
    CHECK(!c->ram[0x3003]);
    if(m==3){CHECK(!r->serial.commands && !r->serial.command_bytes);CHECK(!r->exi.transfers[0]);CHECK(r->stop.raw==input);}
    if(m==4)CHECK(r->serial.command_bytes==split && r->exi.transfers[0]==1);
    if(m==5 || m==6 || m==7 || m==16)CHECK(r->exi.transfers[0]==1 && r->serial.commands==1);
    if(m==6){CHECK(r->serial.output_bytes==MGX_SERIAL_CAPACITY-1);CHECK(r->serial.output[MGX_SERIAL_CAPACITY-1]==0xa5);}
    if(m==7)CHECK(!r->serial.queue_reads);
    if(m==10){CHECK(r->irq.pi_pending==0x10);CHECK(r->serial.commands==1 && r->exi.transfers[0]==1);CHECK(r->irq.event_count==4);}
    if(m==11){CHECK(r->irq.event_count==MGX_SERIAL_MMIO_CAPACITY);CHECK(r->exi.transfers[0]==1 && !r->serial.output_bytes);}
    if(m==12 || m==13)CHECK(!r->irq.event_count);
    free(c->ram);free(c);free(r);
}
#define RUN(m,v,why) one(m,v,MGX_BOOT_WII_SERIAL,why)
int main(void){
    for(alias=0;alias<2;++alias){
        RUN(0,0,"untranslated-address");
        for(split=1;split<4;++split)for(length=1;length<=4;++length){RUN(1,0x41420043,"untranslated-address");RUN(1,0,"untranslated-address");}
        for(length=1;length<=4;++length)RUN(2,0,"untranslated-address");
        uint32_t bad[]={0,0x20000100,0xa0000100,0x30000000,0xb0000001,0xb0000101,0x21000800,0xb0000200};
        for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)RUN(3,bad[i],"unsupported-ipl-command");
        for(split=1;split<4;++split)RUN(4,0,"unsupported-ipl-command-read");
        RUN(5,0xb0000000,"unsupported-euart-direction");RUN(5,0xb0000100,"unsupported-euart-direction");RUN(5,0x30000100,"unsupported-euart-direction");
        RUN(6,0,"serial-output-capacity");RUN(7,0,"serial-output-capacity");
        RUN(8,0,"untranslated-address");RUN(9,0,"untranslated-address");
        RUN(10,0,"unsupported-pending-pi-interrupt");RUN(11,0,"mmio-trace-capacity");
        RUN(12,0,"invalid-serial-state");RUN(13,0,"invalid-serial-state");
        RUN(14,0x180,"unsupported-exi-control-transition");RUN(14,0x300,"unsupported-exi-control-transition");
        RUN(15,0,"untranslated-address");
        uint32_t controls[]={3,7,9,13,0x40,0x10000000};for(unsigned i=0;i<6;++i)RUN(16,controls[i],"unsupported-exi-transfer");
        RUN(17,0,"unimplemented-external-pointer");one(18,0,MGX_BOOT_WII_EXI_PROBE,"unsupported-exi-control-transition");
    }
    printf("PASS: %u synthetic serial cases; %u checks\n",cases,checks);return 0;
}
