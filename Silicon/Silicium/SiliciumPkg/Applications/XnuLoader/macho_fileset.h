/* Minimal Mach-O (MH_FILESET) inspection for the loader: find the kernel's
 * virtual base, total image span, and entry PC. The A12 kernelcache is laid
 * out contiguously (fileoff==vmaddr-vbase, filesize==vmsize), so the loader
 * copies the whole file to physBase and the kernel maps itself. */
#ifndef MACHO_FILESET_H
#define MACHO_FILESET_H
#include "lt.h"

typedef struct {
    uint64_t va_base;      /* lowest segment vmaddr = load VA of byte 0 */
    uint64_t va_end;       /* highest segment vmaddr+vmsize */
    uint64_t span;         /* va_end - va_base (== file size for contiguous kc) */
    uint64_t entry_va;     /* LC_UNIXTHREAD PC */
    uint32_t contiguous;   /* 1 if fileoff/filesize track vmaddr/vmsize for all segs */
} macho_info;

/* Parse header at `img` (whole kernelcache file, `len` bytes). Returns 0 on success. */
int macho_inspect(const void *img, uint64_t len, macho_info *out);

#endif
