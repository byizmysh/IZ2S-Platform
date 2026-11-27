/* SPDX-License-Identifier: ISC
 * Adapted from nx-hbloader/source/trampoline.s at
 * 82b95122c5ae8dc059bf23893ba7623c72c86773.
 * Copyright 2017-2018 nx-hbloader Authors. See ../LICENSE.md.
 * This version returns to a C loader cycle after an NRO return and restores
 * its register file. It is single-flight by design. As upstream's
 * __libnx_exception_entry does, it gives a user-mode exception to the
 * running NRO, whose runtime handles it.
 */
.section .text.hbloaderSwitch2Enter, "ax", %progbits
.align 2
.global hbloaderSwitch2Enter
.type hbloaderSwitch2Enter, %function
hbloaderSwitch2Enter:
    stp x29, x30, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x17, [sp, #-16]!
    stp d8, d9, [sp, #-16]!
    stp d10, d11, [sp, #-16]!
    stp d12, d13, [sp, #-16]!
    stp d14, d15, [sp, #-16]!

    adrp x9, .Lsaved_loader_sp
    add x9, x9, :lo12:.Lsaved_loader_sp
    mov x10, sp
    str x10, [x9]

    adrp x9, .Lactive_nro_entry
    str x2, [x9, :lo12:.Lactive_nro_entry]

    /* x0/x1 are the homebrew ABI arguments. blr places the loader's
     * resume address in x30 for both a normal return and libnx's exit path. */
    mov sp, x3
    blr x2

    adrp x9, .Lactive_nro_entry
    str xzr, [x9, :lo12:.Lactive_nro_entry]

    /* The NRO may leave sp at its own stack top. Resume the loader frame. */
    adrp x9, .Lsaved_loader_sp
    ldr x10, [x9, :lo12:.Lsaved_loader_sp]
    mov sp, x10

    ldp d14, d15, [sp], #16
    ldp d12, d13, [sp], #16
    ldp d10, d11, [sp], #16
    ldp d8, d9, [sp], #16
    ldp x28, x17, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x29, x30, [sp], #16
    ret
.size hbloaderSwitch2Enter, . - hbloaderSwitch2Enter

/* libounce-nx's crt0 calls this when the process entry receives a user-mode
 * exception (x0 its type, x1 its context). Divert it to the running NRO's
 * entry, whose crt0 detects the same arguments; with no NRO, return it to
 * the kernel as unhandled. Upstream: source/trampoline.s. */
.section .text.__libnx_exception_entry, "ax", %progbits
.align 2
.global __libnx_exception_entry
.type __libnx_exception_entry, %function
__libnx_exception_entry:
    adrp x7, .Lactive_nro_entry
    ldr x7, [x7, :lo12:.Lactive_nro_entry]
    cbz x7, 1f
    br x7
1:
    mov w0, #0xf801 /* KERNELRESULT(UnhandledUserInterrupt) */
    svc 0x28        /* svcReturnFromException */
.size __libnx_exception_entry, . - __libnx_exception_entry

.section .bss.hbloaderSwitch2Enter, "aw", %nobits
.align 3
.Lsaved_loader_sp:
    .skip 8
/* The entry of the NRO running now; zero in the loader. */
.Lactive_nro_entry:
    .skip 8
