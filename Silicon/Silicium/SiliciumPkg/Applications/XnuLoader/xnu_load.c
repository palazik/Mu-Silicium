/* XNU loader driver (classic A12 path).
 * The EDK2/Silicium app gathers the inputs below (kernelcache + ADT + ramdisk
 * read into memory, a reserved *contiguous* physical window, and the GOP
 * framebuffer), then calls xnu_load_and_boot(). We lay the images out in the
 * window, build boot_args, and hand off MMU-off with x0=boot_args.
 *
 * Layout in [phys_base .. phys_base+window):
 *   [kernelcache span][ADT][ramdisk][trustcache][boot_args]  (each 16 KiB aligned)
 *   topOfKernelData = end of boot_args; kernel allocates its page tables above.
 *
 * The ramdisk's Apple-signed binaries validate against the stock trust cache,
 * which the kernel reads from /chosen/memory-map/TrustCache (verified: the A12
 * kernel reads the exact key "TrustCache"). We fill that entry here.
 */
#include "macho_fileset.h"
#include "xnu_boot_args.h"
#include "chained_fixups.h"
#include "xnu_load.h"

#define ALIGN16K(x) (((x) + 0x3fffULL) & ~0x3fffULL)

extern void xnu_handoff(uint64_t ba_phys, uint64_t entry_phys,
                        uint64_t flush_base, uint64_t flush_size) __attribute__((noreturn));
/* Renames a /chosen/memory-map "MemoryMapReserved-*" slot to `name` and writes
 * {paddr,size}. Verified against the real n841ap device tree. (adt_memmap.c) */
extern int adt_memmap_set(void *adt, uint64_t len, const char *name,
                          uint64_t paddr, uint64_t size);

static void lcopy(void *d, const void *s, uint64_t n){ char*dd=d; const char*ss=s; while(n--)*dd++=*ss++; }

/* xnu_load_params + xnu_load_and_boot() prototype now live in xnu_load.h,
 * shared with efi_main.c so the two sides of the ABI can't drift. */

/* phys helpers: UEFI identity-maps, so phys == pointer value. */
static void *P(uint64_t pa){ return (void *)(uintptr_t)pa; }

int xnu_load_and_boot(const xnu_load_params *p)
{
    macho_info mi;
    if (macho_inspect(p->kc, p->kc_len, &mi)) return -1;
    if (!mi.contiguous) return -2;   /* this path assumes a contiguous kc (verified for A12) */

    uint64_t base = ALIGN16K(p->phys_base);
    uint64_t kc_phys = base;
    lcopy(P(kc_phys), p->kc, p->kc_len);

    /* The kernelcache is MH_FILESET with DYLD_CHAINED_FIXUPS: its __DATA_CONST/
     * __DATA pointers are packed and must be resolved before the kernel runs.
     * We load at the linked VA (slide 0) and write FLAT pointers for auth
     * fixups; the kernel runs with PAC enforcement disabled (kernelcache built
     * by patch_kernel_a12_m3.py --disable-pac) so its AUTH* leave them intact. */
    uint64_t nfix = 0;
    if (xnu_apply_chained_fixups(P(kc_phys), p->kc_len, 0 /*slide*/, 1, &nfix) != 0)
        return -5;

    uint64_t adt_phys = ALIGN16K(kc_phys + mi.span);
    lcopy(P(adt_phys), p->adt, p->adt_len);

    uint64_t rd_phys = ALIGN16K(adt_phys + p->adt_len);
    if (p->rd && p->rd_len) lcopy(P(rd_phys), p->rd, p->rd_len);
    uint64_t after_rd = ALIGN16K(rd_phys + (p->rd ? p->rd_len : 0));

    uint64_t tc_phys = after_rd;
    if (p->tc && p->tc_len) lcopy(P(tc_phys), p->tc, p->tc_len);
    uint64_t after_tc = ALIGN16K(tc_phys + (p->tc ? p->tc_len : 0));

    /* Fill the device-tree memory-map so the kernel finds the images we placed.
     * Edit the in-RAM ADT copy at adt_phys (not the caller's source). */
    if (p->tc && p->tc_len)
        adt_memmap_set(P(adt_phys), p->adt_len, "TrustCache", tc_phys, p->tc_len);
    if (p->rd && p->rd_len)
        /* Verified against IOKitBSDInit.cpp: the kernel reads /chosen/memory-map
         * property "RAMDisk" = {paddr,size}, does mdevadd()->md0; boot-arg rd=md0
         * mounts it as root. Key is "RAMDisk" (capital D); value is {paddr,size}. */
        adt_memmap_set(P(adt_phys), p->adt_len, "RAMDisk", rd_phys, p->rd_len);

    uint64_t ba_phys = after_tc;
    boot_args *ba = P(ba_phys);
    uint64_t top = ALIGN16K(ba_phys + sizeof(boot_args));

    xnu_boot_inputs in = {
        .virt_base = mi.va_base,
        .phys_base = kc_phys,
        .mem_size  = p->window,
        .top_of_kernel_data = top,
        .mem_size_actual = p->total_ram,
        .fb_base = p->fb_base, .fb_rowbytes = p->fb_rowbytes,
        .fb_width = p->fb_width, .fb_height = p->fb_height, .fb_depth = p->fb_depth,
        .dt_phys = adt_phys, .dt_len = (uint32_t)p->adt_len,
        .cmdline = p->cmdline,
    };
    xnu_build_boot_args(ba, &in);

    uint64_t entry_phys = kc_phys + (mi.entry_va - mi.va_base);
    xnu_handoff(ba_phys, entry_phys, base, top - base);   /* never returns */
    return 0;
}
