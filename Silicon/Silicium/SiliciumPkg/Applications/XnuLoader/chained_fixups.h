/* Resolve LC_DYLD_CHAINED_FIXUPS in an MH_FILESET kernelcache, in place.
 *
 * The A12 (iPhone11,8) kernelcache stores __DATA_CONST/__DATA pointers in the
 * packed dyld chained-fixup format (ptr_format ARM64E_USERLAND, 16K pages,
 * imports_count==0 so every entry is a rebase or auth-rebase, never a bind).
 * Before the kernel runs, each packed slot must be turned into a real pointer.
 *
 * We apply strategy (B): write FLAT (unsigned) pointers for auth fixups and run
 * the kernel with PAC enforcement OFF (patch_kernel_a12_m3.py --disable-pac), so
 * the kernel's runtime AUTH* become NOPs and leave the flat pointers intact.
 */
#ifndef CHAINED_FIXUPS_H
#define CHAINED_FIXUPS_H
#include "lt.h"

/* image:   whole kernelcache file image, already copied into writable memory.
 * len:     image length in bytes.
 * slide:   VA slide applied to the kernel (0 when loaded at its linked base).
 * flatten: must be 1 (write flat pointers, no signing). 0 is reserved for a
 *          future "sign with live keys" path and currently returns an error.
 * nfix_out: if non-NULL, receives the number of fixup slots resolved.
 * Returns 0 on success, negative on a malformed chain. */
int xnu_apply_chained_fixups(void *image, uint64_t len, uint64_t slide,
                             int flatten, uint64_t *nfix_out);

#endif
