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
static uint32_t word_be(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static int executable_range(const mgx_dol_plan *plan,uint32_t address,uint32_t size){
    if(address<GC_RAM_BASE || (uint64_t)address+size>GC_RAM_BASE+0x10000000ull)return 0;
    for(uint32_t i=0;i<plan->count;++i){
        const mgx_dol_section *s=&plan->sections[i];
        if(s->executable && !s->bank && address-GC_RAM_BASE>=s->bank_offset &&
           (uint64_t)address-GC_RAM_BASE+size<=(uint64_t)s->bank_offset+s->size)return 1;
    }
    return 0;
}
static int valid_template(const mgx_code_template *t,const mgx_memory *m,const mgx_dol_plan *p){
    if(!t)return 1;
    if(!t->original || !t->instrumentation_token || !t->size || t->size>4096 ||
       (t->address&3) || (t->size&3) || (t->patch_offset&3) ||
       t->size<4 || t->patch_offset>t->size-4 || t->max_immediate>32767 ||
       !executable_range(p,t->address,t->size))return 0;
    const uint8_t *loaded=mgx_memory_pointer(m,t->address,t->size);
    if(!loaded || memcmp(loaded,t->original,t->size))return 0;
    const uint32_t opcode=word_be(t->original+t->patch_offset);
    /* Primary opcode 14, RA=0, original immediate=0. rD is fixed. */
    if((opcode&0xfc1fffffu)!=0x38000000u || *t->instrumentation_token!=opcode)return 0;
    for(unsigned i=0;i<2;++i){
        if((t->writer_pc[i]&3) || !executable_range(p,t->writer_pc[i],4) ||
           (t->writer_pc[i]>=t->address && t->writer_pc[i]<t->address+t->size))return 0;
        const uint8_t *writer=mgx_memory_pointer(m,t->writer_pc[i],4);
        if(!writer || word_be(writer)!=t->writer_word[i] ||
           (t->writer_word[i]>>26)!=36)return 0; /* stw */
    }
    return 1;
}
static int template_bytes_valid(mgx_execution *r){
    const mgx_code_template *t=r->code_template;
    const uint8_t *p=mgx_memory_pointer(&r->memory,t->address,t->size);
    if(!p || memcmp(p,t->original,t->patch_offset) ||
       memcmp(p+t->patch_offset+4,t->original+t->patch_offset+4,t->size-t->patch_offset-4))return 0;
    const uint32_t v=word_be(p+t->patch_offset),original=word_be(t->original+t->patch_offset);
    return (v&0xffff0000u)==original && (v&0xffffu)<=t->max_immediate;
}
uint32_t mgx_exec_template_li(CPUState *cpu,uint32_t cia){
    mgx_execution *r=run_for(cpu);cpu->pc=cia;
    const mgx_code_template *t=r->code_template;
    if(!t || r->profile!=MGX_BOOT_WII_CPU || cia!=t->address+t->patch_offset)
        stop(r,"unregistered-template-instruction",cia,0,4,0);
    if(!template_bytes_valid(r))stop(r,"invalid-template-instruction",cia,0,4,0);
    const uint8_t *p=mgx_memory_pointer(&r->memory,cia,4);
    ++r->template_instruction_reads;
    /* Only nonnegative signed-16 immediates were admitted above. */
    return word_be(p)&0xffffu;
}
static int template_store(mgx_execution *r,uint32_t address,uint64_t value,uint8_t width){
    const mgx_code_template *t=r->code_template;
    if(!t || r->profile!=MGX_BOOT_WII_CPU || width!=4 ||
       address!=t->address+t->patch_offset)return 0;
    if(r->cpu->pc!=t->writer_pc[0] && r->cpu->pc!=t->writer_pc[1])return 0;
    const uint32_t opcode=word_be(t->original+t->patch_offset);
    if((uint32_t)value>>16!=opcode>>16 || ((uint32_t)value&0xffffu)>t->max_immediate ||
       !template_bytes_valid(r))return 0;
    ++r->template_writes;return 1;
}
/* AOT text stays immutable except for an opt-in instrumented li operand.
   A same-byte RAM store is still performed by
   the caller; it is not elided, so reservation/journal behavior is retained.
   Every byte of a scalar write is compared, even if only part overlaps text. */
static void check_write(CPUState *cpu,uint32_t address,uint64_t value,uint8_t width,void *user){
    mgx_execution *r=user;
    if(cpu!=r->cpu || (width!=1 && width!=2 && width!=4 && width!=8))
        stop(r,"invalid-memory-write",address,value,width,0);
    const uint8_t *before=mgx_memory_pointer(&r->memory,address,width);
    if(!before)stop(r,"unimplemented-memory-write",address,value,width,0);
    const unsigned region=address>>28;
    if(region!=0 && region!=8 && region!=12)return;
    const uint32_t offset=address&0x0fffffffu;
    for(uint32_t i=0;i<r->plan->count;++i){
        const mgx_dol_section *s=&r->plan->sections[i];
        if(!s->executable || s->bank!=0 ||
           (uint64_t)offset+width<=s->bank_offset ||
           (uint64_t)s->bank_offset+s->size<=offset)continue;
        uint8_t bytes[8];
        for(unsigned j=0;j<width;++j)bytes[width-j-1]=(uint8_t)(value>>(j*8));
        if(r->profile!=MGX_BOOT_WII_CPU || memcmp(before,bytes,width)){
            if(template_store(r,offset|GC_RAM_BASE,value,width))return;
            stop(r,"write-to-translated-code",offset|GC_RAM_BASE,value,width,0);
        }
        ++r->identical_code_writes;
        r->last_identical_pc=cpu->pc;r->last_identical_address=offset|GC_RAM_BASE;
        return;
    }
}
static uint64_t read_external(CPUState *cpu,uint32_t address,uint8_t width){
    mgx_execution *r=run_for(cpu);
    const uint8_t *p=mgx_memory_pointer(&r->memory,address,width);
    if(!p)stop(r,"unimplemented-memory-read",address,0,width,0);
    uint64_t v=0;for(unsigned i=0;i<width;++i)v=(v<<8)|p[i];return v;
}
static void write_external(CPUState *cpu,uint32_t address,uint64_t value,uint8_t width){
    mgx_execution *r=run_for(cpu);
    check_write(cpu,address,value,width,r);
    uint8_t *p=mgx_memory_pointer(&r->memory,address,width);
    ppc_clear_reservation_for_store(cpu,address,width);
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
        /* Coherent single-thread RAM: stores are already visible. The one
           permitted template word is executed through a memory-aware AOT
           operand. Copied vectors still have no translation/dispatch binding. */
        ppc_memory_fence();++r->cache_events[operation];return;
    }
    stop(r,"unimplemented-cache-control",address,operation,0,0);
}
void mgx_exec_run_template(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit,
                  mgx_boot_profile profile,const mgx_code_template *code_template){
    if(!r)return;
    memset(r,0,sizeof(*r));r->stop.reason="invalid-execution-arguments";
    if((profile!=MGX_BOOT_STRICT&&profile!=MGX_BOOT_WII_CPU)||!cpu||!memory||!plan||!dispatch||!limit||plan->count>18||cpu->ram!=memory->mem1||
       cpu->ram_size!=memory->mem1_size||cpu->mem2!=memory->mem2||cpu->mem2_size!=memory->mem2_size)return;
    for(uint32_t i=0;i<plan->count;++i)
        if(plan->sections[i].executable&&plan->sections[i].bank!=0)return;
    if(code_template && (profile!=MGX_BOOT_WII_CPU || !valid_template(code_template,memory,plan))){
        r->stop.reason="invalid-template-profile";return;
    }
    r->cpu=cpu;r->memory=*memory;r->plan=plan;r->profile=profile;r->code_template=code_template;
    if(profile==MGX_BOOT_WII_CPU){
        /* Dolphin CBoot::SetupMSR / SetupHID(is_wii=true). Deliberately only
           CPU register presets; low-memory handoff, BATs and IOS are absent. */
        r->hid0=0x0011c664u;cpu->msr=0x00002032u;cpu->hid2=0xe0000000u;
        cpu->runtime_cpu=PPC_RUNTIME_BROADWAY;r->hid4=MGX_HID4_WII_PRESET;
    }
    unsigned old_depth=dolrecomp_call_depth;dolrecomp_call_depth=0;
    PPCMemWriteJournal old_journal=g_mem_write_journal;void *old_user=g_mem_write_journal_user;
    PPCMemWriteCheck old_check=g_mem_write_check;void *old_check_user=g_mem_write_check_user;
    cpu->external_user_data=r;cpu->external_read=read_external;cpu->external_write=write_external;
    cpu->external_pointer=pointer_external;cpu->external_read32=external32;cpu->external_write32=external32_write;
    cpu->instruction_fallback=fallback;cpu->spr_read=read_special;cpu->spr_write=write_special;cpu->cache_control=cache;
    cpu->host_call=NULL;cpu->pc=plan->entry;cpu->exception=0;
    /* The legacy observer has no write value. This one-shot runner temporarily
       replaces it with the value-aware check and restores both global hooks. */
    ppc_set_mem_write_journal(NULL,NULL);ppc_set_mem_write_check(check_write,r);
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
    ppc_set_mem_write_check(old_check,old_check_user);
    ppc_set_mem_write_journal(old_journal,old_user);
    dolrecomp_call_depth=old_depth;
}

void mgx_exec_run_profile(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit,
                  mgx_boot_profile profile){
    mgx_exec_run_template(r,cpu,memory,plan,dispatch,limit,profile,NULL);
}

void mgx_exec_run(mgx_execution *r,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit){
    mgx_exec_run_profile(r,cpu,memory,plan,dispatch,limit,MGX_BOOT_STRICT);
}
