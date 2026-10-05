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
/* Supervisor PMCs are 953,954,957,958 (not a contiguous range).
   Aliases and SIA are intentionally not accepted by this bounded profile. */
static int pmu_counter_index(uint16_t spr){
    switch(spr){case 953:return 0;case 954:return 1;case 957:return 2;case 958:return 3;default:return -1;}
}
static uint32_t read_special(CPUState *cpu,uint16_t spr,uint32_t cia){
    mgx_execution *r=run_for(cpu);cpu->pc=cia;
    if(r->profile==MGX_BOOT_WII_CPU && spr==1008){++r->hid0_reads;return r->hid0;}
    if(r->profile==MGX_BOOT_WII_CPU && spr==1011){++r->hid4_reads;return r->hid4;}
    if(r->profile==MGX_BOOT_WII_CPU && spr==1017){++r->l2_reads;return r->l2cr;}
    if(r->profile==MGX_BOOT_WII_CPU){
        if(spr==952 || spr==956){++r->pmu_reads;return r->pmu_control[spr==956];}
        const int index=pmu_counter_index(spr);
        if(index>=0){++r->pmu_reads;return r->pmu_counter[index];}
    }
    stop(r,"unimplemented-spr-read",spr,0,4,0);return 0;
}
static void write_special(CPUState *cpu,uint16_t spr,uint32_t value,uint32_t cia){
    mgx_execution *r=run_for(cpu);cpu->pc=cia;
    if(r->profile==MGX_BOOT_WII_CPU && spr==1011){
        /* Fixed direct-map CPU profile only. SBE/ST0 and every other bit must
           retain the documented preset. No BAT/page-table/cache transition
           can silently pass through our address helpers. This same-value
           write leaves the mappings and empty/coherent cache unchanged. */
        if(value!=MGX_HID4_WII_PRESET || r->hid4!=MGX_HID4_WII_PRESET)
            stop(r,"unsupported-hid4-transition",spr,value,4,0);
        ppc_memory_fence();++r->hid4_writes;return;
    }
    if(r->profile==MGX_BOOT_WII_CPU && spr==1008){
        /* Only the ordinary cache-enable bits and I-cache invalidate request
           may change. DCFI is deliberately not self-cleared (Dolphin/Gekko
           reference), and changes to it fail: dirty-line loss is unmodelled. */
        const uint32_t permitted=MGX_HID0_ICE|MGX_HID0_DCE|MGX_HID0_ICFI;
        if((r->hid0^value)&~permitted)
            stop(r,"unsupported-hid0-transition",spr,value,4,0);
        ppc_memory_fence();
        if(value&MGX_HID0_ICFI)++r->icache_invalidations;
        r->hid0=value&~MGX_HID0_ICFI;++r->hid0_writes;
        return;
    }
    if(r->profile==MGX_BOOT_WII_CPU && spr==1017){
        const uint32_t enable=0x80000000u,invalidate=0x00200000u;
        /* This CPU-only profile starts with an empty, disabled abstract L2
           (Dolphin ResetRegisters). No SRAM timing/test configuration accepted.
           Invalidation empties an already coherent abstract cache synchronously;
           there is no queued operation, so read-only L2IP remains clear. */
        if(value&~(enable|invalidate|1u))
            stop(r,"unsupported-l2cr-transition",spr,value,4,0);
        if((value&enable) && (value&invalidate))
            stop(r,"invalid-l2-invalidate-while-enabled",spr,value,4,0);
        if((value&invalidate) && !(r->l2cr&invalidate)){
            ppc_memory_fence();++r->l2_invalidations;
        }
        r->l2cr=value&~1u;++r->l2_writes;ppc_memory_fence();return;
    }
    if(r->profile==MGX_BOOT_WII_CPU){
        if(spr==952 || spr==956){
            /* Zero selects no counted events and disables PMU interrupts.
               Reject ANY active/freeze/trigger/reserved configuration before
               publishing state: no unimplemented event can appear to count. */
            if(value)stop(r,"unsupported-pmu-configuration",spr,value,4,0);
            r->pmu_control[spr==956]=value;++r->pmu_control_writes;return;
        }
        const int index=pmu_counter_index(spr);
        if(index>=0){
            /* Invariant: both controls remain zero throughout this run.
               PMCs are still writable/readable storage, never fake cycles. */
            r->pmu_counter[index]=value;++r->pmu_counter_writes;return;
        }
    }
    stop(r,"unimplemented-spr-write",spr,value,4,0);
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
    mgx_execution *r=run_for(cpu);cpu->pc=cia;
    if(r->profile==MGX_BOOT_WII_CPU && operation<=PPC_CACHE_ICBI){
        const uint32_t line=address&~31u;
        if(operation==PPC_CACHE_DCBI){
            /* Only already-invalid locked-cache lines are supported. This
               models the harmless startup disable sequence, not dirty RAM
               invalidation or DMA. Never erase or pretend to flush data. */
            if(line>=0xe0000000u && line<0xe0004000u){
                const uint32_t index=(line-0xe0000000u)>>5;
                if(cpu->locked_cache_valid[index])
                    stop(r,"unsupported-live-locked-cache-invalidate",address,operation,0,0);
                cpu->locked_cache_tag[index]=0;
                ++r->locked_cache_invalidations;
            }else stop(r,"unsupported-data-cache-invalidate",address,operation,0,0);
        }else if(!mgx_memory_pointer(&r->memory,line,32)){
            stop(r,"unsupported-cache-address",address,operation,0,0);
        }
        /* No dirty cache, asynchronous device or writable translated text in
           this profile: RAM stores are already visible; ICBI cannot stale AOT. */
        ppc_memory_fence();++r->cache_events[operation];return;
    }
    stop(r,"unimplemented-cache-control",address,operation,0,0);
}
void mgx_exec_run_profile(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit,
                  mgx_boot_profile profile){
    if(!r)return;
    memset(r,0,sizeof(*r));r->stop.reason="invalid-execution-arguments";
    if((profile!=MGX_BOOT_STRICT&&profile!=MGX_BOOT_WII_CPU)||!cpu||!memory||!plan||!dispatch||!limit||plan->count>18||cpu->ram!=memory->mem1||
       cpu->ram_size!=memory->mem1_size||cpu->mem2!=memory->mem2||cpu->mem2_size!=memory->mem2_size)return;
    for(uint32_t i=0;i<plan->count;++i)
        if(plan->sections[i].executable&&plan->sections[i].bank!=0)return;
    r->cpu=cpu;r->memory=*memory;r->plan=plan;r->profile=profile;
    if(profile==MGX_BOOT_WII_CPU){
        /* Dolphin CBoot::SetupMSR / SetupHID(is_wii=true). Deliberately only
           CPU register presets; low-memory handoff, BATs and IOS are absent. */
        r->hid0=0x0011c664u;cpu->msr=0x00002032u;cpu->hid2=0xe0000000u;
        cpu->runtime_cpu=PPC_RUNTIME_BROADWAY;r->hid4=MGX_HID4_WII_PRESET;
    }
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
            if(r->stop.trace_count<MGX_TRACE_CAPACITY)r->stop.trace[r->stop.trace_count++]=cpu->pc;
            ++r->stop.dispatches;cpu->downcount=0;
            if(!dispatch(cpu,cpu->pc)){r->stop.reason="untranslated-address";break;}
            if(cpu->exception){r->stop.reason="guest-exception";break;}
        }
        r->stop.pc=cpu->pc;
    }
    ppc_set_mem_write_journal(old_journal,old_user);
    dolrecomp_call_depth=old_depth;
}

void mgx_exec_run(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit){
    mgx_exec_run_profile(r,cpu,memory,plan,dispatch,limit,MGX_BOOT_STRICT);
}
