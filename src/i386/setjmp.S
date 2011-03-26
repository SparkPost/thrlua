/* This file is derived from the state-threads md.S file, which has no
 * specific license. It does claim the following, but does not indicate
 * which portions were contributed by SGI.
 *
 * Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
 * All Rights Reserved.
 */

/*
 * Internal __jmp_buf layout
 */
#define JB_BX  0
#define JB_SI  1
#define JB_DI  2
#define JB_BP  3
#define JB_SP  4
#define JB_PC  5

        .text

        /* _momo_setjmp_i386(__jmp_buf env) */
.globl _momo_setjmp_i386
        .align 4
_momo_setjmp_i386:
        movl 4(%esp), %eax

        /*
         * Save registers.
         */
        movl %ebx, (JB_BX*4)(%eax)
        movl %esi, (JB_SI*4)(%eax)
        movl %edi, (JB_DI*4)(%eax)
        /* Save SP */
        leal 4(%esp), %ecx
        movl %ecx, (JB_SP*4)(%eax)
        /* Save PC we are returning to */
        movl 0(%esp), %ecx
        movl %ecx, (JB_PC*4)(%eax)
        /* Save caller frame pointer */
        movl %ebp, (JB_BP*4)(%eax)
        xorl %eax, %eax
        ret

/****************************************************************/

        /* _momo_longjmp_i386(__jmp_buf env, int val) */
.globl _momo_longjmp_i386
        .align 4
_momo_longjmp_i386:
        /* First argument is jmp_buf */
        movl 4(%esp), %ecx
        /* Second argument is return value */
        movl 8(%esp), %eax
        /* Set the return address */
        movl (JB_PC*4)(%ecx), %edx
        /*
         * Restore registers.
         */
        movl (JB_BX*4)(%ecx), %ebx
        movl (JB_SI*4)(%ecx), %esi
        movl (JB_DI*4)(%ecx), %edi
        movl (JB_BP*4)(%ecx), %ebp
        movl (JB_SP*4)(%ecx), %esp
        testl %eax, %eax
        jnz  1f
        incl %eax
        /* Jump to saved PC */
1:      jmp *%edx

/****************************************************************/

