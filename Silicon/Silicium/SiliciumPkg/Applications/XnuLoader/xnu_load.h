/* Shared contract between the EDK2 glue (efi_main.c) and the loader core
 * (xnu_load.c). Keep the struct here so the two sides can never drift (a missing
 * field silently misaligns the ABI -- that bug already happened once). */
#ifndef XNU_LOAD_H
#define XNU_LOAD_H
#include "lt.h"

typedef struct {
    uint64_t phys_base;      /* 16K-aligned start of reserved contiguous RAM */
    uint64_t window;         /* size of reserved RAM (granted to XNU as memSize) */
    uint64_t total_ram;      /* true total RAM for memSizeActual */
    const void *kc;  uint64_t kc_len;    /* kernelcache file image (fixups applied here) */
    const void *adt; uint64_t adt_len;   /* flattened Apple DeviceTree */
    const void *rd;  uint64_t rd_len;    /* restore ramdisk (0 if none this boot) */
    const void *tc;  uint64_t tc_len;    /* stock trust cache for the ramdisk (0 if none) */
    uint64_t fb_base, fb_rowbytes, fb_width, fb_height, fb_depth; /* from GOP */
    const char *cmdline;     /* must include "rd=md0" to root from the RAMDisk md0 */
} xnu_load_params;

/* Applies chained fixups to the kernelcache, lays out [kc][ADT][ramdisk][trustcache]
 * [boot_args] in the window, fills /chosen/memory-map, builds boot_args, and hands
 * off MMU-off with x0=boot_args. Returns negative on error; never returns on success. */
int xnu_load_and_boot(const xnu_load_params *p);

#endif
