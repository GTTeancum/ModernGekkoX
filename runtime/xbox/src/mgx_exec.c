/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include <string.h>
extern unsigned dolrecomp_call_depth;
static void stop(mgx_execution *r,const char *why,uint32_t address,uint64_t value,uint32_t width,uint32_t raw){
    r->stop.reason=why;r->stop.pc=r->cpu->pc;r->stop.address=address;
    r->stop.value=value;r->stop.width=width;r->stop.raw=raw;
    longjmp(r->escape,1);
}
static mgx_execution *run_for(CPUState *cpu){return (mgx_execution *)cpu->external_user_data;}
static void code_write(uint32_t offset,uint32_t width,void *user){
    mgx_execution *r=user;
    for(uint32_t i=0;i<r->plan->count;++i){const mgx_dol_section *s=&r->plan->sections[i];
        if(s->executable&&s->bank==0 && (uint64_t)offset+width>s->bank_offset &&
           (uint64_t)s->bank_offset+s->size>offset)
            stop(r,"write-to-translated-code",offset|GC_RAM_BASE,0,width,0);
    }
}
static uint64_t read_external(CPUState *cpu,uint32_t address,uint8_t width){
    mgx_execution *r=run_for(cpu);
    const uint8_t *p=mgx_memory_pointer(&r->memory,address,width);
    if(!p)stop(r,"unimplemented-memory-read",address,0,width,0);
    uint64_t v=0;for(unsigned i=0;i<width;++i)v=(v<<8)|p[i];return v;
}
static void write_external(CPUState *cpu,uint32_t address,uint64_t value,uint8_t width){
    mgx_execution *r=run_for(cpu);uint8_t *p=mgx_memory_pointer(&r->memory,address,width);
    if(!p)stop(r,"unimplemented-memory-write",address,value,width,0);
    if((address>>28)==0||(address>>28)==8||(address>>28)==12)
        code_write(address & 0x0fffffffu,width,r);
    for(unsigned i=0;i<width;++i)p[width-i-1]=(uint8_t)(value>>(i*8));
}
static void *pointer_external(CPUState *cpu,uint32_t address,uint32_t width){
    stop(run_for(cpu),"unimplemented-external-pointer",address,0,width,0);return NULL;
}
static uint32_t read_special(CPUState *cpu,uint16_t spr,uint32_t cia){
    cpu->pc=cia;stop(run_for(cpu),"unimplemented-spr-read",spr,0,4,0);return 0;
}
static void write_special(CPUState *cpu,uint16_t spr,uint32_t value,uint32_t cia){
    cpu->pc=cia;stop(run_for(cpu),"unimplemented-spr-write",spr,value,4,0);
}
static void fallback(CPUState *cpu,uint32_t raw,uint32_t cia){
    cpu->pc=cia;stop(run_for(cpu),"unimplemented-instruction",0,0,4,raw);
}
static uint32_t external32(CPUState *cpu,uint32_t address,uint8_t rid){
    stop(run_for(cpu),"unimplemented-external-control-read",address,rid,4,0);return 0;
}
static void external32_write(CPUState *cpu,uint32_t address,uint32_t value,uint8_t rid){
    stop(run_for(cpu),"unimplemented-external-control-write",address,value,4,rid);
}
static void cache(CPUState *cpu,uint8_t operation,uint32_t address,uint32_t cia){
    cpu->pc=cia;stop(run_for(cpu),"unimplemented-cache-control",address,operation,0,0);
}
void mgx_exec_run(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit){
    if(!r)return;
    memset(r,0,sizeof(*r));r->stop.reason="invalid-execution-arguments";
    if(!cpu||!memory||!plan||!dispatch||!limit||plan->count>18||cpu->ram!=memory->mem1||
       cpu->ram_size!=memory->mem1_size||cpu->mem2!=memory->mem2||cpu->mem2_size!=memory->mem2_size)return;
    for(uint32_t i=0;i<plan->count;++i)
        if(plan->sections[i].executable&&plan->sections[i].bank!=0)return;
    r->cpu=cpu;r->memory=*memory;r->plan=plan;
    unsigned old_depth=dolrecomp_call_depth;dolrecomp_call_depth=0;
    PPCMemWriteJournal old_journal=g_mem_write_journal;void *old_user=g_mem_write_journal_user;
    cpu->external_user_data=r;cpu->external_read=read_external;cpu->external_write=write_external;
    cpu->external_pointer=pointer_external;cpu->external_read32=external32;cpu->external_write32=external32_write;
    cpu->instruction_fallback=fallback;cpu->spr_read=read_special;cpu->spr_write=write_special;cpu->cache_control=cache;
    cpu->host_call=NULL;cpu->pc=plan->entry;cpu->exception=0;
    ppc_set_mem_write_journal(code_write,r);
    if(!setjmp(r->escape)){
        r->stop.reason="dispatch-budget-exhausted";
        while(r->stop.dispatches<limit){
            if(r->stop.trace_count<32)r->stop.trace[r->stop.trace_count++]=cpu->pc;
            ++r->stop.dispatches;cpu->downcount=0;
            if(!dispatch(cpu,cpu->pc)){r->stop.reason="untranslated-address";break;}
            if(cpu->exception){r->stop.reason="guest-exception";break;}
        }
        r->stop.pc=cpu->pc;
    }
    ppc_set_mem_write_journal(old_journal,old_user);
    dolrecomp_call_depth=old_depth;
}
