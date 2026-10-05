/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_exec.h"
#include "mgx_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
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
    if(argc!=2){fprintf(stderr,"usage: startup-probe main.dol\n");return 2;}
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
    /* Conservative diagnostic entry: zero registers/low memory except loaded
       DOL/BSS. No Wii apploader state or fake OS service results supplied. */
    mgx_math_init();mgx_exec_run(run,cpu,&memory,&plan,mgx_generated_dispatch,10000);
    PRINT("{\"diagnostic\":\"bounded-generated-startup\",\"bootstrap\":\"zero-state-plus-DOL\",\"entry\":\"0x%08lx\",\"stop\":\"%s\",\"pc\":\"0x%08lx\",\"address\":\"0x%08lx\",\"raw\":\"0x%08lx\",\"dispatches\":%lu,\"exception\":%lu,\"gpr1\":\"0x%08lx\",\"lr\":\"0x%08lx\",\"game_booted\":false,\"trace\":[",
          (unsigned long)plan.entry,run->stop.reason,(unsigned long)run->stop.pc,(unsigned long)run->stop.address,(unsigned long)run->stop.raw,(unsigned long)run->stop.dispatches,(unsigned long)cpu->exception,(unsigned long)cpu->gpr[1],(unsigned long)cpu->lr);
    for(uint32_t i=0;i<run->stop.trace_count;++i)PRINT("%s\"0x%08lx\"",i?",":"",(unsigned long)run->stop.trace[i]);
    PRINT("]}\n");cpu_free(cpu);free(cpu);free(run);
#ifdef NXDK
    for(;;)Sleep(1000);
#endif
    return 0;
}
