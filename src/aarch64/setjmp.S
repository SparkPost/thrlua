/* This file is derived from the state-threads md.S file, which has no
 * specific license. It does claim the following, but does not indicate
 * which portions were contributed by SGI.
 *
 * Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

/*
 * Internal __jmp_buf layout for AArch64
 *
 * Callee-saved general-purpose registers: x19-x28, x29 (FP), x30 (LR)
 * Stack pointer: SP
 * Callee-saved SIMD/FP registers: d8-d15
 */
#define JB_X19   0
#define JB_X20   1
#define JB_X21   2
#define JB_X22   3
#define JB_X23   4
#define JB_X24   5
#define JB_X25   6
#define JB_X26   7
#define JB_X27   8
#define JB_X28   9
#define JB_X29  10
#define JB_LR   11
#define JB_SP   12
#define JB_D8   13
#define JB_D9   14
#define JB_D10  15
#define JB_D11  16
#define JB_D12  17
#define JB_D13  18
#define JB_D14  19
#define JB_D15  20

        .text

        /* _lua_setjmp_aarch64(__jmp_buf env) */
.globl _lua_setjmp_aarch64
        .align 4
_lua_setjmp_aarch64:
        /*
         * Save callee-saved general-purpose registers.
         */
        stp x19, x20, [x0, #(JB_X19*8)]
        stp x21, x22, [x0, #(JB_X21*8)]
        stp x23, x24, [x0, #(JB_X23*8)]
        stp x25, x26, [x0, #(JB_X25*8)]
        stp x27, x28, [x0, #(JB_X27*8)]
        stp x29, x30, [x0, #(JB_X29*8)]
        /* Save SP */
        mov x2, sp
        str x2, [x0, #(JB_SP*8)]
        /*
         * Save callee-saved SIMD/FP registers.
         */
        stp d8,  d9,  [x0, #(JB_D8*8)]
        stp d10, d11, [x0, #(JB_D10*8)]
        stp d12, d13, [x0, #(JB_D12*8)]
        stp d14, d15, [x0, #(JB_D14*8)]
        /* Return 0 */
        mov w0, #0
        ret

/****************************************************************/

        /* _lua_longjmp_aarch64(__jmp_buf env, int val) */
.globl _lua_longjmp_aarch64
        .align 4
_lua_longjmp_aarch64:
        /*
         * Restore callee-saved general-purpose registers.
         */
        ldp x19, x20, [x0, #(JB_X19*8)]
        ldp x21, x22, [x0, #(JB_X21*8)]
        ldp x23, x24, [x0, #(JB_X23*8)]
        ldp x25, x26, [x0, #(JB_X25*8)]
        ldp x27, x28, [x0, #(JB_X27*8)]
        ldp x29, x30, [x0, #(JB_X29*8)]
        /* Restore SP */
        ldr x2, [x0, #(JB_SP*8)]
        mov sp, x2
        /*
         * Restore callee-saved SIMD/FP registers.
         */
        ldp d8,  d9,  [x0, #(JB_D8*8)]
        ldp d10, d11, [x0, #(JB_D10*8)]
        ldp d12, d13, [x0, #(JB_D12*8)]
        ldp d14, d15, [x0, #(JB_D14*8)]
        /* Set return value: if val == 0, return 1 instead */
        cmp w1, #0
        csinc w0, w1, wzr, ne
        /* Jump to saved PC (LR was restored into x30 above) */
        ret

/****************************************************************/
