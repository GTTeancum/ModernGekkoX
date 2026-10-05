/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_EXEC_H
#define MGX_EXEC_H
#include "mgx_dol.h"
#include "cpu/cpu.h"
#include <setjmp.h>
/* Bounded diagnostic only: no complete devices, kernel services or fabricated
   success. Discard the CPU after this one-shot run. One thread at a time. */
/* STRICT retains the old all-unsupported stop behavior. WII_CPU is an
   explicitly partial Dolphin-derived CPU preset, not a Wii apploader replay.
   The latter uses coherent single-thread RAM, immutable translated text and
   no devices or pending DMA. Cache timings/dirty-line loss are not emulated. */
/* WII_IRQ extends WII_CPU with an explicit reference interrupt-register
   snapshot. It is not a complete device model or an apploader handoff. */
typedef enum { MGX_BOOT_STRICT, MGX_BOOT_WII_CPU, MGX_BOOT_WII_IRQ } mgx_boot_profile;
#define MGX_MMIO_TRACE_CAPACITY 64u
typedef struct {
    uint32_t pc,address,value,width,is_write;
} mgx_mmio_event;
typedef struct {
    uint32_t pi_cause,pi_mask,pi_pending;
    uint32_t ppc_flags,ppc_mask,mi_mask;
    uint32_t reads,writes,event_count;
    mgx_mmio_event events[MGX_MMIO_TRACE_CAPACITY];
} mgx_boot_irq;
#define MGX_TRACE_CAPACITY 4096u
#define MGX_HID0_ICE  0x00008000u
#define MGX_HID0_DCE  0x00004000u
#define MGX_HID0_ICFI 0x00000800u
#define MGX_HID0_DCFI 0x00000400u
#define MGX_HID4_WII_PRESET 0x83900000u
/* Opt-in parametric AOT template: one addi rD,0,imm word only. The producer
   must replace that instruction at its internal generated label, not merely
   intercept external dispatch. token is defined by the instrumented chunk;
   its unresolved symbol prevents linking the profile with the original chunk.
   Copied vectors are not registered for execution by this API. */
typedef struct {
    uint32_t address,size,patch_offset,max_immediate;
    const uint8_t *original;
    uint32_t writer_pc[2],writer_word[2];
    const uint32_t *instrumentation_token;
} mgx_code_template;
typedef int (*mgx_dispatch)(CPUState *,uint32_t);
typedef struct {
    const char *reason;
    uint32_t pc,address,raw,width,dispatches,trace_count,trace[MGX_TRACE_CAPACITY];
    uint64_t value;
} mgx_stop;
typedef struct {
    CPUState *cpu;
    mgx_memory memory;
    const mgx_dol_plan *plan;
    mgx_stop stop;
    mgx_boot_profile profile;
    uint32_t l2cr,l2_reads,l2_writes,l2_invalidations;
    uint32_t hid4,hid4_reads,hid4_writes;
    uint32_t hid0, hid0_reads, hid0_writes, icache_invalidations;
    uint32_t cache_events[4], locked_cache_invalidations;
    /* MMCR0/MMCR1 and PMC1..4. Only event-disabled control is supported. */
    uint32_t pmu_control[2], pmu_counter[4];
    uint32_t pmu_reads, pmu_control_writes, pmu_counter_writes;
    uint32_t identical_code_writes,last_identical_pc,last_identical_address;
    const mgx_code_template *code_template;
    uint32_t template_writes,template_instruction_reads;
    mgx_boot_irq irq;
    jmp_buf escape;
} mgx_execution;
void mgx_exec_run(mgx_execution *run,CPUState *cpu,const mgx_memory *memory,
                  const mgx_dol_plan *plan,mgx_dispatch dispatch,uint32_t limit);
void mgx_exec_run_profile(mgx_execution *run,CPUState *cpu,const mgx_memory *memory,
                          const mgx_dol_plan *plan,mgx_dispatch dispatch,
                          uint32_t limit,mgx_boot_profile profile);
void mgx_exec_run_template(mgx_execution *run,CPUState *cpu,const mgx_memory *memory,
                          const mgx_dol_plan *plan,mgx_dispatch dispatch,
                          uint32_t limit,mgx_boot_profile profile,
                          const mgx_code_template *code_template);
uint32_t mgx_exec_template_li(CPUState *cpu,uint32_t cia);
#endif
