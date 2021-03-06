/*
 * Copyright 2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *TODO(robertm): find documentation for eabi

 *                  The stack organization
 *                  0(sp)                   argc
 *                  4(sp)                   argv[0]
 *                  ...
 *                  (4*argc)(sp)            NULL
 *                  (4*(argc+1))(sp)        envp[0]
 *                  ...
 *                                          NULL
 */

/*  TODO(robertm): find a header to put this in/get it from, e.g. nacl_config */
     .set ARM_BUNDLE_SIZE, 4
     .set NACL_PAGESHIFT, 12

    .data
    .globl environ

    .text
    .align ARM_BUNDLE_SIZE

    .global _start
_start:

    /* clear frame pointer and link register */
    mov r11, #0
    mov r14, #0

    /* TODO: add the real code here */
    /* register _fini as atexit */

    nop
    bl __pthread_initialize

    /* TODO: add the real code here */
    /* register __pthread_shutdown   as atexit */

    nop
    nop
    nop
    bl _init

    nop
    nop
    nop
    bl __srpc_init

    /* NOTE: we do not have SDL support on arm yet */
    /* call    __av_wait */

    pop {r0}    /* get argc */
    mov r1, sp  /* get argv */

    nop
    bl main

    /* TODO: add the real code here */
    /* push {r0} */
    /* bl __srpc_wait */
    /* pop {r0} */

    nop
    nop
    nop
    bl exit    /* from newlib */

/* ====================================================================== */
/* Some runtime code for ARM */
/* NOTE: this code can only clobber r0 and hence is hand coded */
/* NOTE: this code assumes the "USE_R9_AS_TLS_REG" #define */
/* ====================================================================== */
    .section .text


    .align ARM_BUNDLE_SIZE
    .global __aeabi_read_tp

__aeabi_read_tp:
    mov r0, r9, lsr #NACL_PAGESHIFT
    lsl r0, #NACL_PAGESHIFT
    /* NOTE: it seems that on arm everything is shifted by 8 */
    /* TODO(robertm): check whether this is really or a bug */
    sub r0, r0, #8
    /* This has to wait until new lib is properly sfi compiled */
/* TODO(robertm): fix this */
/*
    bic lr, lr, #0xF000000F
*/
    bx lr


/* NOTE: this is only here to make c++ programs link */
    .align ARM_BUNDLE_SIZE
    .global __aeabi_unwind_cpp_pr0

__aeabi_unwind_cpp_pr0:
/* TODO(robertm): fix this */
/*
    bic lr, lr, #0xF000000F
*/
    bx lr
