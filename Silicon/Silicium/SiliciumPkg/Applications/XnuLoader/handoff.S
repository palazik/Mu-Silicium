/* XNU handoff trampoline (AArch64, EL1).
 * Runs after ExitBootServices with UEFI's identity map active (VA==PA), so this
 * code and stack stay valid once the MMU is off.
 *
 *   void xnu_handoff(uint64_t boot_args_phys,   // x0
 *                    uint64_t kernel_entry_phys,// x1
 *                    uint64_t flush_base,       // x2  (start of region we wrote)
 *                    uint64_t flush_size)       // x3  (kernel+boot_args+DT+ramdisk span)
 *       __attribute__((noreturn));
 *
 * Cleans+invalidates the written region to PoC by VA (so the kernel sees it in
 * RAM after dcache-off), disables MMU+caches, invalidates I-cache/TLB, masks
 * interrupts, and branches to the kernel with x0 = boot_args.
 */
    .text
    .globl xnu_handoff
    .align 2
xnu_handoff:
    mov     x19, x0                 // boot_args phys
    mov     x20, x1                 // kernel entry phys

    // clean+invalidate [x2, x2+x3) by VA, stride = dcache line size
    mrs     x4, ctr_el0
    ubfx    x4, x4, #16, #4         // CTR.DminLine = log2(words per line)
    mov     x5, #4
    lsl     x5, x5, x4              // line bytes = 4 << DminLine
    add     x6, x2, x3              // end
    sub     x7, x5, #1
    bic     x2, x2, x7              // align start down to a line
1:  dc      civac, x2               // clean & invalidate to PoC
    add     x2, x2, x5
    cmp     x2, x6
    b.lo    1b
    dsb     sy
    isb

    // disable MMU + data/instruction caches
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)       // M
    bic     x0, x0, #(1 << 2)       // C
    bic     x0, x0, #(1 << 12)      // I
    msr     sctlr_el1, x0
    isb

    ic      iallu                   // flush I-cache
    dsb     nsh
    tlbi    vmalle1                 // flush TLB
    dsb     nsh
    isb

    msr     daifset, #0xf           // mask D/A/I/F
    mov     x0, x19                 // x0 = boot_args (kernel contract)
    br      x20                     // -> kernel (no return)
