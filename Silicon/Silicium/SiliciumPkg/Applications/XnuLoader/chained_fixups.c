/* See chained_fixups.h. Freestanding C: only <stdint.h>, byte-wise reads so we
 * never depend on struct packing or host alignment. */
#include "chained_fixups.h"

/* ---- little-endian field readers (image is LE arm64) ---- */
static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p){
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static void wr64(uint8_t *p, uint64_t v){ for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* Mach-O / load-command constants */
#define MH_MAGIC_64            0xFEEDFACFu
#define LC_SEGMENT_64          0x19u
#define LC_DYLD_CHAINED_FIXUPS 0x80000034u

/* The A12 kernelcache uses DYLD_CHAINED_PTR_64_KERNEL_CACHE (value 8):
 *   target:30  cacheLevel:2  diversity:16  addrDiv:1  key:2  next:12  isAuth:1
 * i.e. next = bits[62:51] (12b), isAuth = bit63, NO bind bit, 4-byte stride,
 * resolved VA = basePointers[cacheLevel] + target  (cacheLevel 0 => image base).
 * This is the only format we need; others are rejected with a clear code. */
#define PF_64_KERNEL_CACHE     8

int xnu_apply_chained_fixups(void *image, uint64_t len, uint64_t slide,
                             int flatten, uint64_t *nfix_out)
{
    if (nfix_out) *nfix_out = 0;
    if (!flatten) return -100;               /* signing path not implemented */
    if (len < 32) return -1;
    uint8_t *base = (uint8_t *)image;
    if (rd32(base) != MH_MAGIC_64) return -2;

    uint32_t ncmds = rd32(base + 16);
    uint64_t mh_base = ~0ull;                /* lowest segment vmaddr = image base */
    uint32_t cf_off = 0, cf_size = 0;

    uint64_t off = 32;
    for (uint32_t i = 0; i < ncmds; i++) {
        if (off + 8 > len) return -3;
        uint32_t cmd = rd32(base + off), sz = rd32(base + off + 4);
        if (sz < 8 || off + sz > len) return -4;
        if (cmd == LC_SEGMENT_64) {
            uint64_t vmaddr  = rd64(base + off + 24);
            if (vmaddr < mh_base) mh_base = vmaddr;
        } else if (cmd == LC_DYLD_CHAINED_FIXUPS) {
            cf_off  = rd32(base + off + 8);
            cf_size = rd32(base + off + 12);
        }
        off += sz;
    }
    if (cf_off == 0 || cf_size == 0) return -5;    /* nothing to do / not chained */
    if ((uint64_t)cf_off + 32 > len) return -6;
    if (mh_base == ~0ull) return -7;

    const uint8_t *fx = base + cf_off;
    uint32_t hdr_version   = rd32(fx + 0);
    uint32_t starts_offset = rd32(fx + 4);
    uint32_t imports_count = rd32(fx + 16);
    if (hdr_version != 0) return -8;
    if (imports_count != 0) return -9;             /* a bind would need a symbol */

    const uint8_t *sii = fx + starts_offset;       /* dyld_chained_starts_in_image */
    uint32_t seg_count = rd32(sii + 0);
    uint64_t nfix = 0;

    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t seg_info_off = rd32(sii + 4 + 4 * s);
        if (seg_info_off == 0) continue;           /* segment has no fixups */
        const uint8_t *ss = sii + seg_info_off;     /* dyld_chained_starts_in_segment */
        uint16_t page_size   = rd16(ss + 4);
        uint16_t ptr_format  = rd16(ss + 6);
        uint64_t seg_offset  = rd64(ss + 8);        /* file offset of segment start */
        uint16_t page_count  = rd16(ss + 20);
        const uint8_t *page_start = ss + 22;

        if (ptr_format != PF_64_KERNEL_CACHE) return -10;   /* see header */
        if (page_size == 0) return -11;
        const unsigned stride = 4;                          /* kernel-cache stride */

        for (uint16_t pg = 0; pg < page_count; pg++) {
            uint16_t start = rd16(page_start + 2 * pg);
            if (start == 0xFFFF) continue;          /* DYLD_CHAINED_PTR_START_NONE */
            uint64_t cur = seg_offset + (uint64_t)pg * page_size + start;
            for (;;) {
                if (cur + 8 > len) return -12;
                uint8_t *slot = base + cur;
                uint64_t raw = rd64(slot);
                /* DYLD_CHAINED_PTR_64_KERNEL_CACHE: rebase and auth-rebase share
                 * target[29:0] + cacheLevel[31:30]; isAuth (bit63) only selects
                 * whether diversity/key are meaningful. We flatten either way. */
                uint64_t target      = raw & 0x3FFFFFFFull;        /* bits [29:0] */
                uint32_t cache_level = (uint32_t)(raw >> 30) & 3;  /* bits [31:30] */
                if (cache_level != 0) return -14;   /* single-level cache expected */
                wr64(slot, mh_base + slide + target);
                nfix++;

                uint32_t next = (uint32_t)(raw >> 51) & 0xFFF;     /* bits [62:51] */
                if (next == 0) break;
                cur += (uint64_t)next * stride;
            }
        }
    }
    if (nfix_out) *nfix_out = nfix;
    return 0;
}
