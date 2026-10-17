/* XNU arm64 boot_args for the classic (non-SPTM) entry path — A12/iPhone11,8 iOS 18.6.
 * Layout verified against pexpert/arm64/boot.h and the live `start` disassembly
 * (virtBase@0x08, physBase@0x10, memSize@0x18, Video@0x28, deviceTreeP@0x60, bootFlags@0x470).
 * Contract: iBoot/loader jumps to the kernel Mach-O entry with MMU OFF and x0 = &boot_args.
 */
#ifndef XNU_BOOT_ARGS_H
#define XNU_BOOT_ARGS_H
#include "lt.h"

#define BOOT_LINE_LENGTH 1024
#define kBootArgsRevision2 2
#define kBootArgsVersion2  2

typedef struct {
    uint64_t v_baseAddr;   /* framebuffer phys base */
    uint64_t v_display;    /* display code */
    uint64_t v_rowBytes;   /* stride in bytes */
    uint64_t v_width;
    uint64_t v_height;
    uint64_t v_depth;      /* bits/px in low 8; rotate/scale in higher bytes */
} Boot_Video;

typedef struct {
    uint16_t   Revision;
    uint16_t   Version;
    uint64_t   virtBase;        /* kernel virtual base (slide applied) */
    uint64_t   physBase;        /* kernel physical load base */
    uint64_t   memSize;         /* RAM granted to XNU */
    uint64_t   topOfKernelData; /* highest phys addr used by kernel data/boot structs */
    Boot_Video Video;
    uint32_t   machineType;
    uint64_t   deviceTreeP;     /* flattened ADT phys ptr */
    uint32_t   deviceTreeLength;
    char       CommandLine[BOOT_LINE_LENGTH];
    uint64_t   bootFlags;
    uint64_t   memSizeActual;   /* true total RAM (0 => use memSize) */
} boot_args;  /* natural alignment: pad(4) after Version puts virtBase@0x08 */

/* Inputs the loader gathers (GOP for video, its own placement for the rest). */
typedef struct {
    uint64_t virt_base, phys_base, mem_size, top_of_kernel_data, mem_size_actual;
    uint64_t fb_base, fb_rowbytes, fb_width, fb_height, fb_depth;
    uint64_t dt_phys; uint32_t dt_len;
    const char *cmdline;
} xnu_boot_inputs;

void xnu_build_boot_args(boot_args *ba, const xnu_boot_inputs *in);

#endif
