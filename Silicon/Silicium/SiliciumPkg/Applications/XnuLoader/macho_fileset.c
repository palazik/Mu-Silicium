#include "macho_fileset.h"

#define MH_MAGIC_64 0xFEEDFACFu
#define LC_SEGMENT_64 0x19u
#define LC_UNIXTHREAD 0x5u

struct mach_header_64 { uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved; };
struct load_command   { uint32_t cmd, cmdsize; };
struct segment_command_64 {
    uint32_t cmd, cmdsize; char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    int32_t maxprot, initprot; uint32_t nsects, flags;
};

int macho_inspect(const void *img, uint64_t len, macho_info *out)
{
    const struct mach_header_64 *mh = img;
    if (len < sizeof(*mh) || mh->magic != MH_MAGIC_64) return -1;
    uint64_t off = sizeof(*mh), vmin = ~0ull, vmax = 0, entry = 0;
    uint32_t contig = 1;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (off + sizeof(struct load_command) > len) return -2;
        const struct load_command *lc = (const void *)((const char *)img + off);
        if (lc->cmdsize < sizeof(*lc) || off + lc->cmdsize > len) return -3;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *s = (const void *)lc;
            if (s->vmaddr && s->vmsize) {
                if (s->vmaddr < vmin) vmin = s->vmaddr;
                if (s->vmaddr + s->vmsize > vmax) vmax = s->vmaddr + s->vmsize;
            }
        } else if (lc->cmd == LC_UNIXTHREAD) {
            /* arm64 thread state: flavor+count then x0..x28,fp,lr,sp,pc */
            entry = *(const uint64_t *)((const char *)lc + 16 + 32 * 8);
        }
        off += lc->cmdsize;
    }
    if (vmin == ~0ull || !entry) return -4;
    /* contiguous check: for each seg, fileoff must equal vmaddr - vmin */
    off = sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const void *)((const char *)img + off);
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *s = (const void *)lc;
            if (s->vmsize && s->fileoff != (s->vmaddr - vmin)) contig = 0;
            if (s->vmsize && s->filesize != s->vmsize) contig = 0;
        }
        off += lc->cmdsize;
    }
    out->va_base = vmin; out->va_end = vmax; out->span = vmax - vmin;
    out->entry_va = entry; out->contiguous = contig;
    return 0;
}
