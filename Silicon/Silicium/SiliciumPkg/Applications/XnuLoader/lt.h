/* Fixed-width integer types that compile in all three environments this code
 * is built in:
 *   - host (tests) and aarch64-elf freestanding  -> <stdint.h> is present
 *   - EDK2 / Project Mu (often built with -nostdinc) -> fall back to Base.h
 * __has_include selects the right path; the EDK2 ARM toolchains (CLANGDWARF,
 * GCC5) both support __has_include. Only the types actually used by the loader
 * core are provided in the fallback. */
#ifndef LOADER_LT_H
#define LOADER_LT_H

#if defined(__has_include)
#  if __has_include(<stdint.h>)
#    include <stdint.h>
#    define LT_HAVE_STDINT 1
#  endif
#endif

#ifndef LT_HAVE_STDINT
#  include <Base.h>
typedef UINT8  uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef INT32  int32_t;
typedef UINTN  uintptr_t;
#endif

#endif /* LOADER_LT_H */
