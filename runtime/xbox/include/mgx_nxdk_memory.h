/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_NXDK_MEMORY_H
#define MGX_NXDK_MEMORY_H
#include <stdint.h>
/* A measurement, not a promise that an allocation will succeed. */
typedef struct {
    uint32_t total_pages,available_pages,image_pages;
    uint32_t virtual_committed,virtual_reserved;
} mgx_xbox_memory;
#ifdef NXDK
#include <xboxkrnl/xboxkrnl.h>
static inline int mgx_nxdk_query_memory(mgx_xbox_memory *out){
    if(!out)return 0;
    MM_STATISTICS s={0};s.Length=sizeof(s);
    NTSTATUS status=MmQueryStatistics(&s);
    /* Keep the caller's data unchanged if the kernel query fails. */
    if(!NT_SUCCESS(status) || s.AvailablePages>s.TotalPhysicalPages ||
       s.ImagePagesCommitted>s.TotalPhysicalPages)return 0;
    *out=(mgx_xbox_memory){s.TotalPhysicalPages,s.AvailablePages,s.ImagePagesCommitted,
                         s.VirtualMemoryBytesCommitted,s.VirtualMemoryBytesReserved};
    return 1;
}
#endif
/* Lower-bound check only. Heap fragmentation, other allocations, the renderer
   and MEM2 are not accounted for. Query failure must never become a zero-RAM
   measurement; the caller reports the failure separately. */
static inline int mgx_xbox_memory_can_fit(const mgx_xbox_memory *s,uint64_t bytes){
    if(!s || s->available_pages>s->total_pages)return 0;
    return bytes<=((uint64_t)s->available_pages*4096u);
}
#endif
