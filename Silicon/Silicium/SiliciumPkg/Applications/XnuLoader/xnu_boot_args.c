#include "xnu_boot_args.h"

/* compile-time proof the layout matches what `start` reads */
_Static_assert(__builtin_offsetof(boot_args, virtBase)        == 0x08, "virtBase");
_Static_assert(__builtin_offsetof(boot_args, physBase)        == 0x10, "physBase");
_Static_assert(__builtin_offsetof(boot_args, memSize)         == 0x18, "memSize");
_Static_assert(__builtin_offsetof(boot_args, topOfKernelData) == 0x20, "topOfKernelData");
_Static_assert(__builtin_offsetof(boot_args, Video)           == 0x28, "Video");
_Static_assert(__builtin_offsetof(boot_args, machineType)     == 0x58, "machineType");
_Static_assert(__builtin_offsetof(boot_args, deviceTreeP)     == 0x60, "deviceTreeP");
_Static_assert(__builtin_offsetof(boot_args, deviceTreeLength)== 0x68, "deviceTreeLength");
_Static_assert(__builtin_offsetof(boot_args, CommandLine)     == 0x6c, "CommandLine");
_Static_assert(__builtin_offsetof(boot_args, bootFlags)       == 0x470, "bootFlags");
_Static_assert(__builtin_offsetof(boot_args, memSizeActual)   == 0x478, "memSizeActual");
_Static_assert(sizeof(boot_args)                              == 0x480, "sizeof boot_args");

static void bzero_(void *p, unsigned long n){ char *c=p; while(n--) *c++=0; }
static void copy_cmdline(char *dst, const char *src){
    unsigned i=0; if(src){ for(; i<BOOT_LINE_LENGTH-1 && src[i]; i++) dst[i]=src[i]; }
    for(; i<BOOT_LINE_LENGTH; i++) dst[i]=0;
}

void xnu_build_boot_args(boot_args *ba, const xnu_boot_inputs *in)
{
    bzero_(ba, sizeof(*ba));
    ba->Revision = kBootArgsRevision2;
    ba->Version  = kBootArgsVersion2;
    ba->virtBase = in->virt_base;
    ba->physBase = in->phys_base;
    ba->memSize  = in->mem_size;
    ba->topOfKernelData = in->top_of_kernel_data;
    ba->Video.v_baseAddr = in->fb_base;
    ba->Video.v_rowBytes = in->fb_rowbytes;
    ba->Video.v_width    = in->fb_width;
    ba->Video.v_height   = in->fb_height;
    ba->Video.v_depth    = in->fb_depth;   /* bits/px in low byte */
    ba->Video.v_display  = 1;              /* graphics mode initialized */
    ba->machineType = 0;
    ba->deviceTreeP = in->dt_phys;
    ba->deviceTreeLength = in->dt_len;
    copy_cmdline(ba->CommandLine, in->cmdline);
    ba->bootFlags = 0;
    ba->memSizeActual = in->mem_size_actual;
}
