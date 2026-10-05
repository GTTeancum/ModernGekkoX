/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_DOL_H
#define MGX_DOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MGX_OK, MGX_ARGUMENT, MGX_TRUNCATED, MGX_ADDRESS,
    MGX_OVERLAP, MGX_ENTRY, MGX_IO
} mgx_status;

typedef struct {
    uint8_t *mem1;
    uint32_t mem1_size;
    uint8_t *mem2;
    uint32_t mem2_size;
} mgx_memory;

typedef struct {
    uint32_t file_offset, address, size, bank, bank_offset, executable;
} mgx_dol_section;

typedef struct {
    mgx_dol_section sections[18];
    uint32_t count, entry, bss_address, bss_size;
} mgx_dol_plan;

/* read_at must fill the complete requested range or return false. */
typedef bool (*mgx_read_at)(void *user, uint64_t offset, void *out, uint32_t size);

/* No allocations, no MMIO, no dependency on the desktop runtime. */
void *mgx_memory_pointer(const mgx_memory *memory, uint32_t address, uint32_t size);
mgx_status mgx_dol_validate(const uint8_t header[256], uint64_t file_size,
                           const mgx_memory *memory, mgx_dol_plan *out);
/* Validate all metadata before writing. Clear BSS BEFORE loading sections, since
   DOL's BSS envelope can overlap initialized sections. On IO failure, memory may
   be partially written: discard it and do not execute. out is cleared on error. */
mgx_status mgx_dol_load(mgx_read_at read_at, void *user, uint64_t file_size,
                       const mgx_memory *memory, mgx_dol_plan *out);
const char *mgx_status_string(mgx_status status);
#endif
