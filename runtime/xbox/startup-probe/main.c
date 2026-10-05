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
#define PRINT debugPrint
#else
#define PRINT printf
#endif
extern int mgx_generated_dispatch(CPUState *,uint32_t);
static bool read_at(void *u,uint64_t at,void *out,uint32_t n){
    FILE *f=u;return at<=LONG_MAX&&!fseek(f,(long)at,SEEK_SET)&&fread(out,1,n,f)==n;
}
int main(int argc,char **argv){
#ifdef NXDK
    (void)argc;(void)argv;const char *path="D:\\main.dol";XVideoSetMode(640,480,32,REFRESH_DEFAULT);
#else
    if(argc<2||argc>3){fprintf(stderr,"usage: startup-probe main.dol [--strict]\n");return 2;}
    if(argc==3&&strcmp(argv[2],"--strict")){fprintf(stderr,"unknown startup option\n");return 2;}
    const char *path=argv[1];
#endif
    FILE *f=fopen(path,"rb");if(!f)return 3;
    if(fseek(f,0,SEEK_END)||ftell(f)<0){fclose(f);return 4;}
    uint64_t size=(uint64_t)ftell(f);
    CPUState *cpu=calloc(1,sizeof(*cpu));mgx_execution *run=calloc(1,sizeof(*run));
    if(!cpu||!run||!cpu_init(cpu)){fclose(f);free(cpu);free(run);return 5;}
    mgx_memory memory={cpu->ram,cpu->ram_size,cpu->mem2,cpu->mem2_size};mgx_dol_plan plan;
    mgx_status loaded=mgx_dol_load(read_at,f,size,&memory,&plan);fclose(f);
    if(loaded!=MGX_OK){PRINT("loader rejected input: %s\n",mgx_status_string(loaded));cpu_free(cpu);free(cpu);free(run);return 6;}
    mgx_boot_profile profile=MGX_BOOT_WII_CPU;
#ifndef NXDK
    if(argc==3)profile=MGX_BOOT_STRICT;
#endif
    /* CPU preset only; apploader, low-memory handoff and devices unimplemented. */
    mgx_math_init();mgx_exec_run_profile(run,cpu,&memory,&plan,mgx_generated_dispatch,10000,profile);
    PRINT("{\"diagnostic\":\"bounded-generated-startup\",\"bootstrap\":\"%s\",\"entry\":\"0x%08lx\",\"stop\":\"%s\",\"pc\":\"0x%08lx\",\"address\":\"0x%08lx\",\"raw\":\"0x%08lx\",\"dispatches\":%lu,\"exception\":%lu,\"gpr1\":\"0x%08lx\",\"lr\":\"0x%08lx\",\"game_booted\":false,\"trace\":[",
          profile==MGX_BOOT_WII_CPU?"dolphin-wii-cpu-only":"zero-state-plus-DOL",(unsigned long)plan.entry,run->stop.reason,(unsigned long)run->stop.pc,(unsigned long)run->stop.address,(unsigned long)run->stop.raw,(unsigned long)run->stop.dispatches,(unsigned long)cpu->exception,(unsigned long)cpu->gpr[1],(unsigned long)cpu->lr);
    for(uint32_t i=0;i<run->stop.trace_count;++i)PRINT("%s\"0x%08lx\"",i?",":"",(unsigned long)run->stop.trace[i]);
    PRINT("],\"hid0\":\"0x%08lx\",\"hid0_reads\":%lu,\"hid0_writes\":%lu,\"icache_invalidations\":%lu,\"cache_events\":[%lu,%lu,%lu,%lu],\"locked_cache_invalidations\":%lu,\"trace_truncated\":%s,\"value\":\"0x%08lx\",\"l2cr\":\"0x%08lx\",\"l2_reads\":%lu,\"l2_writes\":%lu,\"l2_invalidations\":%lu",
          (unsigned long)run->hid0,(unsigned long)run->hid0_reads,(unsigned long)run->hid0_writes,
          (unsigned long)run->icache_invalidations,(unsigned long)run->cache_events[0],
          (unsigned long)run->cache_events[1],(unsigned long)run->cache_events[2],
          (unsigned long)run->cache_events[3],(unsigned long)run->locked_cache_invalidations,
          run->stop.dispatches>run->stop.trace_count?"true":"false",(unsigned long)run->stop.value,(unsigned long)run->l2cr,(unsigned long)run->l2_reads,
          (unsigned long)run->l2_writes,(unsigned long)run->l2_invalidations);
    PRINT(",\"pmu_control\":[\"0x%08lx\",\"0x%08lx\"],\"pmu_counter\":[\"0x%08lx\",\"0x%08lx\",\"0x%08lx\",\"0x%08lx\"],\"pmu_reads\":%lu,\"pmu_control_writes\":%lu,\"pmu_counter_writes\":%lu,\"srr0\":\"0x%08lx\",\"srr1\":\"0x%08lx\",\"msr\":\"0x%08lx\"}\n",
          (unsigned long)run->pmu_control[0],(unsigned long)run->pmu_control[1],
          (unsigned long)run->pmu_counter[0],(unsigned long)run->pmu_counter[1],
          (unsigned long)run->pmu_counter[2],(unsigned long)run->pmu_counter[3],
          (unsigned long)run->pmu_reads,(unsigned long)run->pmu_control_writes,
          (unsigned long)run->pmu_counter_writes,(unsigned long)cpu->srr0,
          (unsigned long)cpu->srr1,(unsigned long)cpu->msr);
    cpu_free(cpu);free(cpu);free(run);
#ifdef NXDK
    for(;;)Sleep(1000);
#endif
    return 0;
}
