/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include "mgx_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#ifdef NXDK
#include <hal/video.h>
#include <hal/debug.h>
#include <windows.h>
#include "mgx_nxdk_memory.h"
#define PRINT debugPrint
#else
#define PRINT printf
#endif
#ifdef MGX_TEMPLATE_PROFILE_HEADER
#include MGX_TEMPLATE_PROFILE_HEADER
#else
#define MGX_TEMPLATE_PROFILE ((const mgx_code_template *)0)
#endif
extern int mgx_generated_dispatch(CPUState *,uint32_t);
static bool read_at(void *u,uint64_t at,void *out,uint32_t n){
    FILE *f=u;return at<=LONG_MAX&&!fseek(f,(long)at,SEEK_SET)&&fread(out,1,n,f)==n;
}
int main(int argc,char **argv){
#ifdef NXDK
    (void)argc;(void)argv;const char *path="D:\\main.dol";XVideoSetMode(640,480,32,REFRESH_DEFAULT);
#else
    if(argc<2){fprintf(stderr,"usage: startup-probe main.dol [--strict | --cpu-only | --irq-only | --audio-only | --exi-only] [--dump-mem1 path]\n");return 2;}
    const char *path=argv[1],*dump_path=NULL;mgx_boot_profile requested_profile=MGX_BOOT_WII_EXI_PROBE;int profile_requested=0;
    for(int i=2;i<argc;++i){
        if(!strcmp(argv[i],"--strict")&&!profile_requested){requested_profile=MGX_BOOT_STRICT;profile_requested=1;}
        else if(!strcmp(argv[i],"--cpu-only")&&!profile_requested){requested_profile=MGX_BOOT_WII_CPU;profile_requested=1;}
        else if(!strcmp(argv[i],"--irq-only")&&!profile_requested){requested_profile=MGX_BOOT_WII_IRQ;profile_requested=1;}
        else if(!strcmp(argv[i],"--audio-only")&&!profile_requested){requested_profile=MGX_BOOT_WII_AUDIO;profile_requested=1;}
        else if(!strcmp(argv[i],"--exi-only")&&!profile_requested){requested_profile=MGX_BOOT_WII_EXI;profile_requested=1;}
        else if(!strcmp(argv[i],"--dump-mem1")&&!dump_path&&i+1<argc)dump_path=argv[++i];
        else {fprintf(stderr,"unknown or incomplete startup option\n");return 2;}
    }
    /* Never overwrite the input DOL with a diagnostic snapshot. Canonical
       path/alias checking is handled by the private invoking tool as well. */
    if(dump_path&&!strcmp(path,dump_path)){fprintf(stderr,"dump path equals input\n");return 2;}
#endif
    FILE *f=fopen(path,"rb");if(!f)return 3;
    if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return 4;}
    uint64_t size=(uint64_t)ftell(f);
#ifdef NXDK
    mgx_xbox_memory memory_before={0};
    if(mgx_nxdk_query_memory(&memory_before)){
        PRINT("Xbox RAM: total_pages=%lu available_pages=%lu image_pages=%lu\n",
              (unsigned long)memory_before.total_pages,(unsigned long)memory_before.available_pages,
              (unsigned long)memory_before.image_pages);
        uint64_t required=(uint64_t)GC_MAIN_RAM_SIZE+sizeof(CPUState)+sizeof(mgx_execution);
        if(!mgx_xbox_memory_can_fit(&memory_before,required)){
            PRINT("Insufficient free physical RAM for complete MEM1 diagnostic; not starting guest.\n");
            fclose(f);for(;;)Sleep(1000);
        }
    }else PRINT("Xbox RAM query failed; available memory is unknown.\n");
#endif
    CPUState *cpu=calloc(1,sizeof(*cpu));mgx_execution *run=calloc(1,sizeof(*run));
    if(!cpu||!run||!cpu_init(cpu)){fclose(f);free(cpu);free(run);return 5;}
    mgx_memory memory={cpu->ram,cpu->ram_size,cpu->mem2,cpu->mem2_size};mgx_dol_plan plan;
    mgx_status loaded=mgx_dol_load(read_at,f,size,&memory,&plan);fclose(f);
    if(loaded!=MGX_OK){PRINT("loader rejected input: %s\n",mgx_status_string(loaded));cpu_free(cpu);free(cpu);free(run);return 6;}
    mgx_boot_profile profile=MGX_BOOT_WII_EXI_PROBE;
#ifndef NXDK
    profile=requested_profile;
#endif
    /* Explicit reference preset; no authentic apploader or complete devices. */
    mgx_math_init();mgx_exec_run_template(run,cpu,&memory,&plan,mgx_generated_dispatch,10000,profile,profile!=MGX_BOOT_STRICT?MGX_TEMPLATE_PROFILE:NULL);
    PRINT("{\"diagnostic\":\"bounded-generated-startup\",\"bootstrap\":\"%s\",\"entry\":\"0x%08lx\",\"stop\":\"%s\",\"pc\":\"0x%08lx\",\"address\":\"0x%08lx\",\"raw\":\"0x%08lx\",\"dispatches\":%lu,\"exception\":%lu,\"gpr1\":\"0x%08lx\",\"lr\":\"0x%08lx\",\"game_booted\":false,\"trace\":[",
          profile==MGX_BOOT_WII_EXI_PROBE?"reference-wii-exi-absent-sp1":profile==MGX_BOOT_WII_EXI?"reference-wii-exi-no-cards":profile==MGX_BOOT_WII_AUDIO?"reference-wii-audio-idle":profile==MGX_BOOT_WII_IRQ?"reference-wii-irq-init":profile==MGX_BOOT_WII_CPU?"dolphin-wii-cpu-only":"zero-state-plus-DOL",(unsigned long)plan.entry,run->stop.reason,(unsigned long)run->stop.pc,(unsigned long)run->stop.address,(unsigned long)run->stop.raw,(unsigned long)run->stop.dispatches,(unsigned long)cpu->exception,(unsigned long)cpu->gpr[1],(unsigned long)cpu->lr);
    for(uint32_t i=0;i<run->stop.trace_count;++i)PRINT("%s\"0x%08lx\"",i?",":"",(unsigned long)run->stop.trace[i]);
    PRINT("],\"hid0\":\"0x%08lx\",\"hid0_reads\":%lu,\"hid0_writes\":%lu,\"icache_invalidations\":%lu,\"cache_events\":[%lu,%lu,%lu,%lu],\"locked_cache_invalidations\":%lu,\"trace_truncated\":%s,\"value\":\"0x%08lx\",\"l2cr\":\"0x%08lx\",\"l2_reads\":%lu,\"l2_writes\":%lu,\"l2_invalidations\":%lu",
          (unsigned long)run->hid0,(unsigned long)run->hid0_reads,(unsigned long)run->hid0_writes,
          (unsigned long)run->icache_invalidations,(unsigned long)run->cache_events[0],
          (unsigned long)run->cache_events[1],(unsigned long)run->cache_events[2],
          (unsigned long)run->cache_events[3],(unsigned long)run->locked_cache_invalidations,
          run->stop.dispatches>run->stop.trace_count?"true":"false",(unsigned long)run->stop.value,(unsigned long)run->l2cr,(unsigned long)run->l2_reads,
          (unsigned long)run->l2_writes,(unsigned long)run->l2_invalidations);
    PRINT(",\"pmu_control\":[\"0x%08lx\",\"0x%08lx\"],\"pmu_counter\":[\"0x%08lx\",\"0x%08lx\",\"0x%08lx\",\"0x%08lx\"],\"pmu_reads\":%lu,\"pmu_control_writes\":%lu,\"pmu_counter_writes\":%lu,\"srr0\":\"0x%08lx\",\"srr1\":\"0x%08lx\",\"msr\":\"0x%08lx\",\"gpr3\":\"0x%08lx\",\"hid4\":\"0x%08lx\",\"hid4_reads\":%lu,\"hid4_writes\":%lu",
          (unsigned long)run->pmu_control[0],(unsigned long)run->pmu_control[1],
          (unsigned long)run->pmu_counter[0],(unsigned long)run->pmu_counter[1],
          (unsigned long)run->pmu_counter[2],(unsigned long)run->pmu_counter[3],
          (unsigned long)run->pmu_reads,(unsigned long)run->pmu_control_writes,
          (unsigned long)run->pmu_counter_writes,(unsigned long)cpu->srr0,
          (unsigned long)cpu->srr1,(unsigned long)cpu->msr,
          (unsigned long)cpu->gpr[3],(unsigned long)run->hid4,
          (unsigned long)run->hid4_reads,(unsigned long)run->hid4_writes);
    PRINT(",\"identical_code_writes\":%lu,\"last_identical_pc\":\"0x%08lx\",\"last_identical_address\":\"0x%08lx\",\"write_width\":%lu,\"write_value64\":\"0x%08lx%08lx\"",
          (unsigned long)run->identical_code_writes,(unsigned long)run->last_identical_pc,
          (unsigned long)run->last_identical_address,(unsigned long)run->stop.width,
          (unsigned long)(uint32_t)(run->stop.value>>32),(unsigned long)(uint32_t)run->stop.value);
    PRINT(",\"template_writes\":%lu,\"template_instruction_reads\":%lu",(unsigned long)run->template_writes,(unsigned long)run->template_instruction_reads);
    PRINT(",\"pi_cause\":\"0x%08lx\",\"pi_mask\":\"0x%08lx\",\"pi_pending\":\"0x%08lx\",\"ppc_irq_mask\":\"0x%08lx\",\"ppc_irq_flags\":\"0x%08lx\",\"mi_irq_mask\":\"0x%08lx\",\"mmio_reads\":%lu,\"mmio_writes\":%lu,\"mmio_events\":[",
          (unsigned long)run->irq.pi_cause,(unsigned long)run->irq.pi_mask,
          (unsigned long)run->irq.pi_pending,(unsigned long)run->irq.ppc_mask,
          (unsigned long)run->irq.ppc_flags,(unsigned long)run->irq.mi_mask,
          (unsigned long)run->irq.reads,(unsigned long)run->irq.writes);
    for(unsigned i=0;i<run->irq.event_count;++i){
        const mgx_mmio_event *ev=&run->irq.events[i];
        PRINT("%s{\"pc\":\"0x%08lx\",\"address\":\"0x%08lx\",\"value\":\"0x%08lx\",\"width\":%lu,\"write\":%s}",i?",":"",
              (unsigned long)ev->pc,(unsigned long)ev->address,(unsigned long)ev->value,
              (unsigned long)ev->width,ev->is_write?"true":"false");
    }
    PRINT("]");
    PRINT(",\"dsp_control\":\"0x%08lx\",\"ai_control\":\"0x%08lx\",\"dsp_control_reads\":%lu,\"dsp_control_writes\":%lu,\"ai_control_reads\":%lu,\"ai_control_writes\":%lu",
          (unsigned long)run->audio.dsp_control,(unsigned long)run->audio.ai_control,
          (unsigned long)run->audio.dsp_reads,(unsigned long)run->audio.dsp_writes,
          (unsigned long)run->audio.ai_reads,(unsigned long)run->audio.ai_writes);
    PRINT(",\"exi_status\":[\"0x%08lx\",\"0x%08lx\",\"0x%08lx\"],\"exi_reads\":[%lu,%lu,%lu],\"exi_writes\":[%lu,%lu,%lu]",
          (unsigned long)run->exi.status[0],(unsigned long)run->exi.status[1],(unsigned long)run->exi.status[2],
          (unsigned long)run->exi.reads[0],(unsigned long)run->exi.reads[1],(unsigned long)run->exi.reads[2],
          (unsigned long)run->exi.writes[0],(unsigned long)run->exi.writes[1],(unsigned long)run->exi.writes[2]);
    PRINT(",\"exi_probe_immediate\":\"0x%08lx\",\"exi_probe_transfers\":%lu,\"exi_probe_bytes\":%lu",
          (unsigned long)run->exi.immediate[0],(unsigned long)run->exi.transfers[0],(unsigned long)run->exi.transfer_bytes[0]);
    PRINT(",\"di_config\":\"0x%08lx\",\"di_config_reads\":%lu",(unsigned long)run->di_config,(unsigned long)run->di_config_reads);
    PRINT(",\"gpr\":[");
    for(unsigned i=0;i<32;++i)PRINT("%s\"0x%08lx\"",i?",":"",(unsigned long)cpu->gpr[i]);
    const uint8_t *before=mgx_memory_pointer(&memory,run->stop.address,4);
    uint32_t before_word=before?((uint32_t)before[0]<<24)|((uint32_t)before[1]<<16)|((uint32_t)before[2]<<8)|before[3]:0;
    PRINT("],\"stop_address_backed\":%s,\"stop_address_word\":\"0x%08lx\"}\n",before?"true":"false",(unsigned long)before_word);
    int result_code=0;
#ifndef NXDK
    if(dump_path){
        /* Exclusive creation: aliases/symlinks/existing input or evidence files
           cannot be truncated. The invoking tool selects a fresh output. */
        FILE *out=fopen(dump_path,"wbx");
        if(!out){fprintf(stderr,"cannot create MEM1 snapshot\n");result_code=7;}
        else {
            int ok=fwrite(memory.mem1,1,memory.mem1_size,out)==memory.mem1_size;
            if(fclose(out))ok=0;
            if(!ok){remove(dump_path);fprintf(stderr,"MEM1 snapshot write failed\n");result_code=7;}
        }
    }
#endif
    cpu_free(cpu);free(cpu);free(run);
#ifdef NXDK
    for(;;)Sleep(1000);
#endif
    return result_code;
}
