/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks,mode,alias,continued,length,direction;
static uint32_t input,address;static uint64_t value;static uint8_t width;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"EXI probe case%u mode%u line%d: %s\n",cases,mode,__LINE__,#x);exit(1);}}while(0)
static uint32_t base(void){return alias?0xcd006800u:0x0d006800u;}
static int dispatch(CPUState*c,uint32_t pc){
 (void)pc;mgx_execution*r=c->external_user_data;uint32_t b=base();
 c->reserve_valid=1;c->reserve_addr=0x80003000u;
 switch(mode){
 case 0:{ /* Exercise both directions, every bus length, and nonzero payloads. */
  mem_write32(c,b,0x200u);CHECK(mem_read32(c,b)==0xa00u);
  mem_write32(c,b+16,input);CHECK(mem_read32(c,b+16)==input);
  uint32_t v=((length-1u)<<4)|(direction<<2)|1u;
  mem_write32(c,b+12,v);CHECK(mem_read32(c,b+12)==(v&~1u));
  CHECK(mem_read32(c,b+16)==(direction?input:0u));
  CHECK(r->exi.transfers[0]==1 && r->exi.transfer_bytes[0]==length);
  CHECK(mem_read32(c,b)==0xa08u && !r->irq.pi_pending);
  mem_write32(c,b,0x200u);CHECK(mem_read32(c,b)==0xa08u); /* writing0 retains TCINT */
  mem_write32(c,b,0x208u);CHECK(mem_read32(c,b)==0xa00u);
  CHECK(r->irq.events[4].value==v && r->irq.events[4].is_write);
  CHECK(!r->exi.transfers[1] && !r->exi.transfers[2]);return 0;
 }
 case 1:mem_write32(c,b,0x200u);c->external_write(c,b+12,value,width);break;
 case 2:mem_write32(c,b,0x200u);c->external_write(c,b+16,value,width);break;
 case 3:c->external_read(c,address,width);break;
 case 4:c->external_write(c,address,value,width);break;
 case 5: /* Complete IRQ-generating shift: commit state, then stop. */
  mem_write32(c,0xcc003004,0x10u);mem_write32(c,b,0x204u);
  mem_write32(c,b+12,0x31u);break;
 case 6: /* TCINT can remain masked by PI, then W1C deasserts source. */
  mem_write32(c,b,0x204u);mem_write32(c,b+12,0x31u);
  CHECK(r->irq.pi_cause==0x10110u && !r->irq.pi_pending);
  mem_write32(c,b,0x20cu);CHECK(!(r->irq.pi_cause&0x10u));return 0;
 case 7: /* No start means register storage, not a completed transfer. */
  mem_write32(c,b+12,(uint32_t)value);CHECK(r->exi.control[0]==value);
  CHECK(!r->exi.transfers[0] && r->exi.status[0]==0x800u);return 0;
 case 8:
  mem_write32(c,b,0x200u);
  for(unsigned i=1;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,b);
  mem_write32(c,b+12,0x31u);break;
 case 9:r->exi.status[0]|=(uint32_t)value;c->external_read(c,b,4);break;
 case 10:r->exi.control[0]=(uint32_t)value;c->external_read(c,b+12,4);break;
 case 11:mem_write32(c,b,0x200u);mem_write32(c,b+12,0x31u);mem_write32(c,b,0x140u);break;
 case 12:r->irq.pi_cause|=0x10;c->external_read(c,b,4);break;
 case 13:{
  uint32_t a=alias?0xcd006024u:0x0d006024u;
  CHECK(mem_read32(c,a)==1u && r->di_config_reads==1 && r->di_config==1);
  CHECK(r->irq.events[0].address==a && r->irq.events[0].value==1);return 0;
 }
 case 14:r->di_config=0;mem_read32(c,0xcd006024);break;
 case 15:
  for(unsigned i=0;i<MGX_MMIO_TRACE_CAPACITY;++i)(void)mem_read32(c,b);
  mem_read32(c,0xcd006024);break;
 case 16:mem_write32(c,b,0x200u);c->external_pointer(c,b+16,4);break;
 }
 ++continued;mem_write32(c,0x80003000,0xbad);return 0;
}
static void one(unsigned m,uint64_t v,unsigned w,uint32_t a,mgx_boot_profile profile,const char*why){
 ++cases;mode=m;value=v;width=w;address=a;continued=0;
 CPUState*c=calloc(1,sizeof(*c));mgx_execution*r=calloc(1,sizeof(*r));CHECK(c&&r);
 c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram);
 mgx_memory memory={c->ram,c->ram_size,NULL,0};mgx_dol_plan p={0};p.entry=0x80001000;p.count=1;
 p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
 mgx_exec_run_profile(r,c,&memory,&p,dispatch,2,profile);
 if(strcmp(r->stop.reason,why))fprintf(stderr,"expected %s got %s\n",why,r->stop.reason);
 CHECK(!strcmp(r->stop.reason,why));CHECK(!continued && !c->exception && c->reserve_valid);
 CHECK(!c->ram[0x3003]);CHECK(r->stop.pc==0x80001000);
 if(m==5){CHECK(r->exi.transfers[0]==1 && r->exi.transfer_bytes[0]==4);CHECK(r->exi.control[0]==0x30);CHECK(r->irq.pi_pending==0x10 && r->irq.event_count==3);}
 if(m==1 || m==2){CHECK(r->exi.status[0]==0xa00u);CHECK(r->irq.event_count==1);CHECK(!r->exi.transfers[0] && !r->exi.control[0] && !r->exi.immediate[0]);}
 if(m==8){CHECK(r->irq.event_count==64 && !r->exi.transfers[0]);CHECK(r->exi.status[0]==0xa00u && !r->exi.control[0]);}
 if(m==11){CHECK(r->exi.status[0]==0xa08u && r->exi.transfers[0]==1 && r->exi.control[0]==0x30u);CHECK(r->irq.event_count==2);}
 if(m==3 || m==4 || m==9 || m==10 || m==12 || m==14)CHECK(!r->irq.event_count);
 if(m==14 || m==15)CHECK(!r->di_config_reads);
 if(m==15)CHECK(r->irq.event_count==64);
 for(unsigned i=0;i<r->irq.event_count;++i)CHECK(r->irq.events[i].pc==0x80001000);
 free(c->ram);free(c);free(r);
}
#define RUN(m,v,w,a,why) one(m,v,w,a,MGX_BOOT_WII_EXI_PROBE,why)
int main(void){
 for(alias=0;alias<2;++alias){
  for(length=1;length<=4;++length)for(direction=0;direction<2;++direction)
   for(unsigned seed=0;seed<4;++seed){input=seed==0?0:seed==1?0xffffffffu:seed==2?0x1234abcd:0x80000001;RUN(0,0,4,0,"untranslated-address");}
  for(unsigned v=0;v<64;++v){
   if((v&2u) || ((v>>2)&3u)>1u)RUN(1,v,4,0,"unsupported-exi-transfer");
   else if(!(v&1u))RUN(7,v,4,0,"untranslated-address");
  }
  for(unsigned bit=6;bit<32;++bit)RUN(1,UINT64_C(1)<<bit,4,0,"unsupported-exi-transfer");
  RUN(1,UINT64_C(0x100000000),4,0,"unsupported-exi-transfer");RUN(2,UINT64_C(0x100000000),4,0,"unsupported-exi-data-width");
  unsigned ws[]={0,1,2,3,8};for(unsigned i=0;i<5;++i){RUN(1,0x31,ws[i],0,"unsupported-mmio-width");RUN(2,1,ws[i],0,"unsupported-mmio-width");RUN(3,0,ws[i],base()+16,"unsupported-mmio-width");RUN(3,0,ws[i],alias?0xcd006024:0x0d006024,"unsupported-mmio-width");}
  RUN(5,0,4,0,"unsupported-pending-pi-interrupt");RUN(6,0,4,0,"untranslated-address");
  RUN(8,0,4,0,"mmio-trace-capacity");RUN(11,0,4,0,"unsupported-exi-control-transition");RUN(12,0,4,0,"inconsistent-exi-interrupt-state");
  RUN(9,0x100,4,0,"unsupported-active-exi-state");RUN(9,0x280,4,0,"unsupported-active-exi-state");RUN(9,0x1000,4,0,"unsupported-active-exi-state");RUN(9,2,4,0,"unsupported-active-exi-state");
  uint32_t bad[]={1,2,8,12,0x80000000};for(unsigned i=0;i<5;++i)RUN(10,bad[i],4,0,"unsupported-active-exi-state");
  RUN(13,0,4,0,"untranslated-address");RUN(14,0,4,0,"unsupported-di-configuration");RUN(15,0,4,0,"mmio-trace-capacity");RUN(16,0,4,0,"unimplemented-external-pointer");
  RUN(4,1,4,alias?0xcd006024:0x0d006024,"write-to-readonly-di-config");
  for(unsigned selection=0;selection<8;++selection)if(selection!=0 && selection!=1 && selection!=4)RUN(4,selection<<7,4,base(),"unsupported-exi-control-transition");
  RUN(4,0x31,4,base()+12,"unsupported-exi-transfer"); /* No selected endpoint. */
  RUN(3,0,4,base()+17,"unsupported-mmio-width");RUN(4,0,4,base()+17,"unsupported-mmio-width");
 }
 uint32_t invalid[]={0xcc006810,0x8d006810,0x4d006810,0x0d006824,0x0d006838,0xcd006804,0xcd006808,0xcd006020,0xcd006028,0xcc006024};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i){RUN(3,0,4,invalid[i],"unimplemented-memory-read");RUN(4,0,4,invalid[i],"unimplemented-memory-write");}
 for(unsigned profile=0;profile<=MGX_BOOT_WII_EXI;++profile){one(3,0,4,0xcd006810,(mgx_boot_profile)profile,"unimplemented-memory-read");one(3,0,4,0xcd006024,(mgx_boot_profile)profile,"unimplemented-memory-read");}
 printf("PASS: %u synthetic exi-probe cases; %u checks\n",cases,checks);return 0;
}
