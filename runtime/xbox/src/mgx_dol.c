/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mgx_dol.h"
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static bool resolve(const mgx_memory *m, uint32_t a, uint32_t n,
                    uint32_t *bank, uint32_t *offset) {
    uint32_t region = a >> 28;
    uint32_t o = a & 0x0fffffffu;
    uint32_t size;
    const uint8_t *p;
    if (!m || !n) return false;
    if (region == 0 || region == 8 || region == 12) {
        *bank = 0; size = m->mem1_size; p = m->mem1;
        if (size > 24u * 1024u * 1024u) return false;
    } else if (region == 1 || region == 9 || region == 13) {
        *bank = 1; size = m->mem2_size; p = m->mem2;
        if (size > 64u * 1024u * 1024u) return false;
    } else return false;
    if (!p || o > size || n > size - o) return false;
    *offset = o;
    return true;
}

void *mgx_memory_pointer(const mgx_memory *m, uint32_t a, uint32_t n) {
    uint32_t bank, offset;
    if (!resolve(m, a, n, &bank, &offset)) return NULL;
    return (bank ? m->mem2 : m->mem1) + offset;
}

mgx_status mgx_dol_validate(const uint8_t h[256], uint64_t file_size,
                           const mgx_memory *m, mgx_dol_plan *out) {
    mgx_dol_plan p;
    uint32_t bank, offset, eb, eo;
    bool entry_ok = false;
    if (!out) return MGX_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!h || !m) return MGX_ARGUMENT;
    if (file_size < 256) return MGX_TRUNCATED;
    memset(&p, 0, sizeof(p));
    p.entry = be32(h + 0xe0);
    p.bss_address = be32(h + 0xd8);
    p.bss_size = be32(h + 0xdc);
    if (p.bss_size && !resolve(m, p.bss_address, p.bss_size, &bank, &offset))
        return MGX_ADDRESS;
    for (uint32_t i = 0; i < 18; ++i) {
        uint32_t n = be32(h + 0x90 + 4 * i);
        if (!n) continue;
        mgx_dol_section *s = &p.sections[p.count];
        s->file_offset = be32(h + 4 * i);
        s->address = be32(h + 0x48 + 4 * i);
        s->size = n; s->executable = i < 7;
        if (s->file_offset < 256 || (uint64_t)s->file_offset > file_size ||
            n > file_size - s->file_offset) return MGX_TRUNCATED;
        if (!resolve(m, s->address, n, &s->bank, &s->bank_offset)) return MGX_ADDRESS;
        for (uint32_t j = 0; j < p.count; ++j) {
            const mgx_dol_section *v = &p.sections[j];
            if (s->bank == v->bank && s->bank_offset < v->bank_offset + v->size &&
                v->bank_offset < s->bank_offset + n) return MGX_OVERLAP;
        }
        ++p.count;
    }
    if ((p.entry & 3u) || !resolve(m, p.entry, 4, &eb, &eo)) return MGX_ENTRY;
    for (uint32_t i = 0; i < p.count; ++i) {
        const mgx_dol_section *s = &p.sections[i];
        if (s->executable && s->bank == eb && eo >= s->bank_offset &&
            (uint64_t)eo + 4 <= (uint64_t)s->bank_offset + s->size) entry_ok = true;
    }
    if (!entry_ok) return MGX_ENTRY;
    *out = p;
    return MGX_OK;
}

mgx_status mgx_dol_load(mgx_read_at read_at, void *user, uint64_t file_size,
                       const mgx_memory *m, mgx_dol_plan *out) {
    uint8_t h[256];
    mgx_dol_plan p;
    mgx_status status;
    if (!out) return MGX_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!read_at || !m) return MGX_ARGUMENT;
    if (file_size < sizeof(h)) return MGX_TRUNCATED;
    if (!read_at(user, 0, h, sizeof(h))) return MGX_IO;
    status = mgx_dol_validate(h, file_size, m, &p);
    if (status != MGX_OK) return status;
    if (p.bss_size) memset(mgx_memory_pointer(m, p.bss_address, p.bss_size), 0, p.bss_size);
    for (uint32_t i = 0; i < p.count; ++i) {
        const mgx_dol_section *s = &p.sections[i];
        if (!read_at(user, s->file_offset, mgx_memory_pointer(m, s->address, s->size), s->size))
            return MGX_IO;
    }
    *out = p;
    return MGX_OK;
}

const char *mgx_status_string(mgx_status s) {
    static const char *const names[] = {
        "ok", "invalid argument", "truncated image", "unbacked address",
        "overlapping sections", "invalid entry point", "read failed"
    };
    return (unsigned)s < sizeof(names)/sizeof(names[0]) ? names[s] : "unknown status";
}
