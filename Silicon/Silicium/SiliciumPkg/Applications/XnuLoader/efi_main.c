/* EDK2 UEFI application entry for the XNU-on-SM8750 loader (A12 classic path).
 *
 * NOTE: this is the EDK2 glue skeleton. It targets the standard UEFI protocols
 * and is meant to drop into a Project Silicium (EDK2) app .inf. It is written
 * against edk2 headers (Uefi.h, Library/*), which aren't present in this repo,
 * so it is NOT compiled here — unlike loader_core.o which is fully verified.
 * Review the TODOs before building in your Silicium tree.
 *
 * Flow: load the four images from the ESP/FAT (kernelcache, device tree, restore
 * ramdisk, trust cache), allocate a contiguous 16K-aligned window, read the GOP
 * mode, ExitBootServices, then xnu_load_and_boot() -- which applies the chained
 * fixups to the kernelcache, lays out the images, builds boot_args + the
 * /chosen/memory-map entries, and hands off MMU-off with x0=boot_args.
 */
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>

#include "macho_fileset.h"
#include "xnu_boot_args.h"
#include "xnu_load.h"   /* xnu_load_params + xnu_load_and_boot(), shared with the core */

/* --- tiny FAT/ESP file reader via SimpleFileSystem (opened on our device) --- */
static EFI_STATUS read_file(EFI_FILE_PROTOCOL *dir, CHAR16 *name, void **buf, UINTN *len)
{
    EFI_FILE_PROTOCOL *f; EFI_STATUS st;
    st = dir->Open(dir, &f, name, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) return st;
    /* size via GetInfo(FileInfo) */
    EFI_FILE_INFO *fi; UINTN isz = sizeof(EFI_FILE_INFO) + 256;
    gBS->AllocatePool(EfiLoaderData, isz, (void **)&fi);
    f->GetInfo(f, &gEfiFileInfoGuid, &isz, fi);
    *len = fi->FileSize;
    gBS->FreePool(fi);
    st = gBS->AllocatePool(EfiLoaderData, *len, buf);
    if (EFI_ERROR(st)) { f->Close(f); return st; }
    st = f->Read(f, len, *buf);
    f->Close(f);
    return st;
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS st;
    EFI_LOADED_IMAGE_PROTOCOL *li;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void **)&li);
    gBS->HandleProtocol(li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&fs);
    fs->OpenVolume(fs, &root);

    void *kc, *adt, *rd = NULL, *tc = NULL; UINTN kc_len, adt_len, rd_len = 0, tc_len = 0;
    /* strategy-B kernelcache: m1+m2+m3 with --disable-pac (flat fixups, PAC off).
     * xnu_load_and_boot() applies the chained fixups to this image at load time. */
    st = read_file(root, L"kernelcache.a12.m3.nopac.patched", &kc, &kc_len);
    if (EFI_ERROR(st)) { Print(L"kc load failed %r\n", st); return st; }
    st = read_file(root, L"DeviceTree.a12.adt", &adt, &adt_len);
    if (EFI_ERROR(st)) { Print(L"adt load failed %r\n", st); return st; }
    /* restore ramdisk (raw, im4p-stripped) -> md0, and its stock trust cache. */
    read_file(root, L"ramdisk.a12.raw", &rd, &rd_len);
    read_file(root, L"trustcache.a12.raw", &tc, &tc_len);

    macho_info mi;
    if (macho_inspect(kc, kc_len, &mi)) { Print(L"bad kernelcache\n"); return EFI_LOAD_ERROR; }

    /* contiguous 16K-aligned window: kc + adt + ramdisk + boot_args + headroom for the
     * kernel's own boot page tables. Grant a generous window as memSize. */
    UINTN window = (UINTN)(mi.span + adt_len + rd_len + tc_len + 0x100000 /*boot_args+slack*/ + (64u<<20));
    window = (window + 0x3fffu) & ~0x3fffu;
    EFI_PHYSICAL_ADDRESS base = 0;
    /* AllocateAnyPages gives 4K-aligned; request extra and round up to 16K inside. */
    UINTN pages = (window + 0x4000) / EFI_PAGE_SIZE;
    st = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &base);
    if (EFI_ERROR(st)) { Print(L"alloc %r\n", st); return st; }

    gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop);
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *gi = gop->Mode->Info;

    xnu_load_params p;
    SetMem(&p, sizeof(p), 0);
    p.phys_base   = base;
    p.window      = window;
    p.total_ram   = window;               /* TODO: set from the UEFI memory map total */
    p.kc = kc; p.kc_len = kc_len;
    p.adt = adt; p.adt_len = adt_len;
    p.rd = rd; p.rd_len = rd_len;
    p.tc = tc; p.tc_len = tc_len;
    p.fb_base     = gop->Mode->FrameBufferBase;    /* Silicium GOP = SM8750 cont_splash fb */
    p.fb_rowbytes = gi->PixelsPerScanLine * 4;
    p.fb_width    = gi->HorizontalResolution;
    p.fb_height   = gi->VerticalResolution;
    p.fb_depth    = 32;
    /* MUST root from the RAM disk; MUST NOT set serial=/debug= (would make
     * serial_init probe the absent Apple UART and fault -- see NOTES serial finding). */
    p.cmdline     = "rd=md0 -v";

    /* exit boot services (retry once on the standard EFI_INVALID_PARAMETER map-key race) */
    UINTN msz = 0, mapkey = 0, dsz = 0; UINT32 dver = 0; EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    gBS->GetMemoryMap(&msz, mmap, &mapkey, &dsz, &dver);
    msz += 2 * dsz;
    gBS->AllocatePool(EfiLoaderData, msz, (void **)&mmap);
    gBS->GetMemoryMap(&msz, mmap, &mapkey, &dsz, &dver);
    st = gBS->ExitBootServices(ImageHandle, mapkey);
    if (EFI_ERROR(st)) {
        gBS->GetMemoryMap(&msz, mmap, &mapkey, &dsz, &dver);
        st = gBS->ExitBootServices(ImageHandle, mapkey);
    }
    /* point of no return: UEFI services are gone; identity map still active. */
    xnu_load_and_boot(&p);   /* never returns on success */
    return EFI_LOAD_ERROR;
}
