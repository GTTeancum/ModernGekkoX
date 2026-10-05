/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_dol.h"
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

_Static_assert(sizeof(void *) == 4, "The loader probe must target 32-bit Xbox");
static bool read_at(void *user, uint64_t offset, void *out, uint32_t size) {
    FILE *f = (FILE *)user;
    return offset <= LONG_MAX && fseek(f, (long)offset, SEEK_SET) == 0 &&
           fread(out, 1, size, f) == size;
}
int main(void) {
    mgx_memory memory = {0};
    mgx_dol_plan plan;
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugPrint("ModernGekkoX: DOL loader diagnostic\n");
    debugPrint("This is NOT a game boot. Guest execution is disabled.\n");
    FILE *f = fopen("D:\\main.dol", "rb");
    if (!f) {
        debugPrint("No D:\\main.dol found. Place your extracted DOL beside this XBE.\n");
    } else if (fseek(f, 0, SEEK_END) != 0 || ftell(f) < 0) {
        debugPrint("Cannot determine DOL size.\n");
    } else {
        uint64_t file_size = (uint64_t)ftell(f);
        memory.mem1_size = 24u * 1024u * 1024u;
        memory.mem1 = (uint8_t *)calloc(1, memory.mem1_size);
        if (!memory.mem1) {
            debugPrint("MEM1 allocation failed.\n");
        } else {
            mgx_status status = mgx_dol_load(read_at, f, file_size, &memory, &plan);
            debugPrint("Loader result: %s\n", mgx_status_string(status));
            if (status == MGX_OK)
                debugPrint("%lu sections; entry 0x%08lx. Not executed.\n",
                           (unsigned long)plan.count, (unsigned long)plan.entry);
        }
    }
    if (f) fclose(f);
    free(memory.mem1);
    debugPrint("Diagnostic finished. Guest code was not run.\n");
    for (;;) Sleep(1000);
    return 0;
}
