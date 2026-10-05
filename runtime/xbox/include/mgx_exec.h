/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_EXEC_H
#define MGX_EXEC_H
#include "mgx_dol.h"
#include "cpu/cpu.h"
#include <setjmp.h>
/* Bounded diagnostic only: no device/kernel-service emulation or fabricated
   success. Discard the CPU after this one-shot run. One thread at a time. */
typedef int (*mgx_dispatch)(CPUState *,uint32_t);
typedef struct {
    const char *reason;
    uint32_t pc,address,raw,width,dispatches,trace_count,trace[32];
    uint64_t value;
} mgx_stop;
typedef struct {
    CPUState *cpu;
    mgx_memory memory;
    const mgx_dol_plan *plan;
    mgx_stop stop;
    jmp_buf escape;
} mgx_execution;
void mgx_exec_run(mgx_execution *run,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit);
#endif
