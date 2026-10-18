// SPDX-License-Identifier: MIT
#include "lnk.h"

static const uint8_t LNK_CLSID[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};

enum {
    HAS_ID_LIST = 0x1,
    HAS_LINK_INFO = 0x2,
    HAS_NAME = 0x4,
    HAS_REL_PATH = 0x8,
    HAS_WORKDIR = 0x10,
    HAS_ARGS = 0x20,
    HAS_ICON = 0x40,
    IS_UNICODE = 0x80,
};

static uint32_t u16at(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32at(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// String terminada em zero dentro de [p, end). ANSI vira Latin-1 (aproximação suficiente
// para comparar caminhos).
static WCHAR *cstr_at(const uint8_t *p, const uint8_t *end, bool unicode) {
    StrBuf b = {0};
    if (unicode) {
        while (p + 1 < end && (p[0] || p[1])) sb_addc(&b, (WCHAR)u16at(p)), p += 2;
    } else {
        while (p < end && *p) sb_addc(&b, *p), p++;
    }
    return sb_take(&b);
}

bool lnk_parse(const uint8_t *data, size_t size, LnkInfo *out) {
    mem_set(out, 0, sizeof(*out));
    if (size < 76 || u32at(data) != 0x4C || mem_cmp(data + 4, LNK_CLSID, 16) != 0) return false;
    const uint8_t *end = data + size;
    uint32_t flags = u32at(data + 20);
    bool unicode = (flags & IS_UNICODE) != 0;
    size_t off = 76;

    if (flags & HAS_ID_LIST) {
        if (off + 2 > size) return false;
        off += 2 + u16at(data + off);
    }
    if (flags & HAS_LINK_INFO) {
        if (off + 28 > size) return false;
        const uint8_t *li = data + off;
        uint32_t li_size = u32at(li), li_hdr = u32at(li + 4), li_flags = u32at(li + 8);
        if (li_size < 28 || off + li_size > size) return false;
        const uint8_t *li_end = li + li_size;
        if (li_flags & 1) {  // VolumeIDAndLocalBasePath
            uint32_t base_off = u32at(li + 16), suffix_off = u32at(li + 24);
            bool use_unicode = li_hdr >= 0x24 && li_size >= 36;
            if (use_unicode) {
                uint32_t bu = u32at(li + 28), su = u32at(li + 32);
                if (bu && bu < li_size) {
                    WCHAR *base = cstr_at(li + bu, li_end, true), *suf = (su && su < li_size) ? cstr_at(li + su, li_end, true) : wdup(u"");
                    out->target = wcat(base, suf);
                    xfree(base), xfree(suf);
                }
            }
            if (!out->target && base_off && base_off < li_size) {
                WCHAR *base = cstr_at(li + base_off, li_end, false), *suf = (suffix_off && suffix_off < li_size) ? cstr_at(li + suffix_off, li_end, false) : wdup(u"");
                out->target = wcat(base, suf);
                xfree(base), xfree(suf);
            }
        }
        off += li_size;
    }
    // StringData: cada string é um contador de caracteres (16 bits) seguido dos caracteres
    static const uint32_t order[] = {HAS_NAME, HAS_REL_PATH, HAS_WORKDIR, HAS_ARGS, HAS_ICON};
    for (size_t i = 0; i < COUNT(order); i++) {
        if (!(flags & order[i])) continue;
        if (off + 2 > size) break;
        uint32_t count = u16at(data + off);
        size_t bytes = (size_t)count * (unicode ? 2 : 1);
        off += 2;
        if (off + bytes > size) break;
        StrBuf b = {0};
        for (uint32_t k = 0; k < count; k++) sb_addc(&b, unicode ? (WCHAR)u16at(data + off + 2 * k) : data[off + k]);
        WCHAR *s = sb_take(&b);
        if (order[i] == HAS_WORKDIR) out->workdir = s;
        else if (order[i] == HAS_ARGS) out->args = s;
        else if (order[i] == HAS_ICON) out->icon = s;
        else xfree(s);
        off += bytes;
    }
    (void)end;
    if (out->target && !out->target[0]) xfree(out->target), out->target = NULL;
    return true;
}

void lnk_free(LnkInfo *l) {
    xfree(l->target), xfree(l->workdir), xfree(l->args), xfree(l->icon);
    mem_set(l, 0, sizeof(*l));
}
