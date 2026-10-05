/* SPDX-License-Identifier: GPL-3.0-or-later
 * Event-disabled performance-monitor contract. Synthetic data only. */
#include "mgx_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned which, checks, continued, calls;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL case %u line %u: %s\n",which,__LINE__,#x); exit(1); } } while(0)
static const uint16_t controls[2]={952,956};
static const uint16_t counters[4]={953,954,957,958};
static int dispatch(CPUState *c,uint32_t address){
    mgx_execution *r=c->external_user_data;
    (void)address; ++calls;
    if(which==0){
        for(unsigned i=0;i<2;++i){
            CHECK(ppc_mfspr(c,controls[i],0x80001000)==0);
            ppc_mtspr(c,controls[i],0,0x80001004);
        }
        for(unsigned i=0;i<4;++i){
            CHECK(ppc_mfspr(c,counters[i],0x80001008)==0);
            uint32_t value=0xdeadbeefu-i*0x11111111u;
            ppc_mtspr(c,counters[i],value,0x8000100c);
            CHECK(ppc_mfspr(c,counters[i],0x80001010)==value);
            for(unsigned k=0;k<i;++k)CHECK(r->pmu_counter[k]==0xdeadbeefu-k*0x11111111u);
        }
        /* Resetting controls must not clear counter storage. */
        for(unsigned i=0;i<2;++i)ppc_mtspr(c,controls[i],0,0x80001014);
        for(unsigned i=0;i<4;++i)CHECK(r->pmu_counter[i]==0xdeadbeefu-i*0x11111111u);
        CHECK(r->pmu_control_writes==4 && r->pmu_counter_writes==4);
        return 0;
    }
    if(which==1){
        if(calls==1)for(unsigned i=0;i<4;++i)ppc_mtspr(c,counters[i],0x80000000u+i,0x80001004);
        else for(unsigned i=0;i<4;++i)CHECK(ppc_mfspr(c,counters[i],0x80001008)==0x80000000u+i);
        return calls<3; /* Unrelated dispatches must not become fake PMU cycles. */
    }
    if(which>=2 && which<=7){
        const uint32_t rejected[]={1,0x40,0x8000,0x80000000,0x80000000,0xffffffff};
        const unsigned k=which-2;
        ppc_mtspr(c,counters[0],0x12345678,0x80001000);
        ppc_mtspr(c,controls[k>=4],rejected[k],0x80001004);
    }else if(which>=8 && which<=13){
        const unsigned k=which-8;const uint16_t spr=k<2?controls[k]:counters[k-2];
        c->msr|=0x4000;
        ppc_mtspr(c,spr,0,0x80001004);
        CHECK(c->exception==PPC_EXC_PROGRAM);
        CHECK(r->pmu_control_writes==0 && r->pmu_counter_writes==0);
        return 1;
    }else if(which>=14 && which<=19){
        const unsigned k=which-14;const uint16_t spr=k<2?controls[k]:counters[k-2];
        c->msr|=0x4000;(void)ppc_mfspr(c,spr,0x80001004);
        CHECK(c->exception==PPC_EXC_PROGRAM && r->pmu_reads==0);return 1;
    }else if(which==20){ppc_mtspr(c,952,0,0x80001004);}
    else if(which==21){(void)ppc_mfspr(c,953,0x80001004);}
    else if(which==22){ppc_mtspr(c,955,0,0x80001004);} /* SIA unsupported */
    else if(which==23){(void)ppc_mfspr(c,937,0x80001004);} /* User alias unsupported */
    else if(which==24){ppc_mtspr(c,937,1,0x80001004);}
    ++continued;return 1;
}
int main(void){
    for(which=0;which<25;++which){
        CPUState *c=calloc(1,sizeof(*c));CHECK(c!=NULL);
        c->ram_size=0x4000;c->ram=calloc(1,c->ram_size);CHECK(c->ram!=NULL);
        mgx_memory m={c->ram,c->ram_size,NULL,0};mgx_dol_plan p;memset(&p,0,sizeof(p));
        p.count=1;p.entry=0x80001000;p.sections[0]=(mgx_dol_section){256,0x80001000,0x100,0,0x1000,1};
        mgx_execution r;memset(&r,0xff,sizeof(r));continued=calls=0;
        mgx_boot_profile profile=(which==20 || which==21)?MGX_BOOT_STRICT:MGX_BOOT_WII_CPU;
        mgx_exec_run_profile(&r,c,&m,&p,dispatch,5,profile);
        const char *want=which<2?"untranslated-address":which<8?"unsupported-pmu-configuration":
            which<20?"guest-exception":which==20?"unimplemented-spr-write":
            which==21?"unimplemented-spr-read":which==22?"unimplemented-spr-write":
            which==23?"unimplemented-spr-read":"unimplemented-spr-write";
        CHECK(!strcmp(r.stop.reason,want));CHECK(continued==0);
        CHECK(r.pmu_control[0]==0 && r.pmu_control[1]==0);
        if(which>=2 && which<8){
            CHECK(r.stop.pc==0x80001004 && r.pmu_control_writes==0);
            CHECK(r.pmu_counter[0]==0x12345678 && r.pmu_counter_writes==1);
        }
        if(which>=8)CHECK(r.pmu_control_writes==0&&r.pmu_counter_writes==0&&r.pmu_reads==0);
        CHECK(r.stop.dispatches==(which==1?3:1));
        free(c->ram);free(c);
    }
    printf("PASS: 25 synthetic inactive-PMU cases; %u checks\n",checks);return 0;
}
