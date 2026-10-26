/***************************************************************************//**
 * @file
 * @brief Implementation of fast memcpy.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include <string.h>
#include "em_device.h"
#include "sl_compiler.h"

#if !defined(__CORTEXM)
#error "The sl_memcpy component is not supported for this device."
#endif

#if defined(__clang__) || defined(__GNUC__)
// Wrapping a system function works by using the linker option '--wrap=symbol'.
// Any undefined reference to "symbol" will be resolved to "__wrap_symbol".
// Any undefined reference to "__real_symbol" will be resolved to "symbol".
#define STD_LIB_WRAPPER_MEMCPY  __wrap_memcpy

// Coverage/profile instrumentation (-fprofile-arcs) injects counter-increment
// code at function entry that clobbers r3/r4/r5 before our inline assembly
// runs. Because this is a naked function and the short-data path does not
// push/pop r4-r11, the corrupted callee-saved registers leak back to the
// caller and violate AAPCS. Prevent that by opting this translation unit's
// hot function out of profile instrumentation.
#define ATTR_NO_PROFILE __attribute__((no_profile_instrument_function))

#if defined(__clang__)
// LLVM/Clang uses the 'used' attribute to prevent the linker from stripping the symbol during LTO.
#define ATTR_EXT_VIS    __attribute__((used))
#else
// The GNU gold linker has an issue with LTO and wrapping, where the symbol is
// stripped even if it is used in the source code, which leads to link-time errors.
// By marking the wrapper as externally_visible, the symbol will not be stripped
// from the final binary, regardless if it is referenced or not in the source code.
#define ATTR_EXT_VIS    __attribute__((externally_visible))
#endif

#elif defined(__IAR_SYSTEMS_ICC__)
// Wrapping a system function works with IAR by patching symbol definitions using $Super$$ and $Sub$$
// The $Sub$$ special pattern identifies the new function that is called instead of the
// original function.
// IAR use different memset implementation depending on the optimization settings.
#define STD_LIB_WRAPPER_MEMCPY        $Sub$$__aeabi_memcpy
#define STD_LIB_WRAPPER_MEMCPY_SIZE   $Sub$$__iar_small_memcpy

// Since IAR does not use LTO, resolve the attribute as nothing.
#define ATTR_EXT_VIS

// IAR does not support gcov-style instrumentation in this configuration.
#define ATTR_NO_PROFILE

#else
#error "Unsupported compiler for standard C memory functions retarget."
#endif

/*******************************************************************************
 * @brief   Fast memcpy using ARM assembly. Copies a number of bytes from the
 *          source to the destination.
 *
 * @param[out] dst  Destination address.
 * @param[in]  src  Source address.
 * @param[in]  len  Number of bytes to copy.
 *
 * @return  Destination address.
 *
 * @note    To get the best performance, the source and the destination must
 *          have the same word-alignment.
 *          If the source and the destination overlap, the behavior is undefined.
 *
 * @details Register assignment:
 *  r0:     dst (output pointer)
 *  r1:     src (input pointer)
 *  r2:     len (number of bytes to copy)
 *  r3:     number of chunks to copy
 *  r4-r11: buffer registers
 *  r12:    amount of bytes to shift during memcpy_with_realign
 ******************************************************************************/
ATTR_NO_PROFILE __NO_PROLOGUE void* sl_memcpy(void *dst, const void *src, size_t len)
{
  __ASM volatile(
    /* Function entry - save retval. */
    "push    {r0, lr}\n"
    /* Check size, do bytewise immediately for small data (< 8 bytes). */
    "cmp     r2, #8\n"
    "bmi     _asm_memcpy_bytewise_nopop\n"
    /* Save callee-saved registers since we'll use them. */
    "push    {r4-r11}\n"
    /* Check alignment, do multiwordwise copy if already aligned. */
    "orr     r3, r0, r1\n"
    "ands    r3, r3, #3\n"
    "beq     _asm_memcpy_octawise\n"
    /* Copy bytewise until input is word-aligned (leave offset in r12). */
    "and     r12, r1, #3\n"
    "rsb     r12, r12, #4\n"
    "sub     r2, r2, r12\n"
    "_asm_memcpy_align_loop:\n"
    "ldrb    r3, [r1], #1\n"
    "strb    r3, [r0], #1\n"
    "subs    r12, r12, #1\n"
    "bne     _asm_memcpy_align_loop\n"
    /* Check if output is now also word-aligned. */
    "ands    r12, r0, #3\n"
    "bne     _asm_memcpy_with_realign\n"
    /*
     * Octaword copy, most optimal for throughput.
     * Adjust in/out addresses as the data op goes, keep len updated manually.
     */
    "_asm_memcpy_octawise:\n"
    "lsrs    r3, r2, #5\n"
    "beq    _asm_memcpy_doublewise\n"
    "_asm_memcpy_octawise_loop:\n"
    "ldmia   r1!, {r4-r11}\n"
    "stmia   r0!, {r4-r11}\n"
    "subs    r3, r3, #1\n"
    "bne     _asm_memcpy_octawise_loop\n"
    "and     r2, r2, #31\n"
    /*
     * Double-wordwise copy: use ldmia/stmia with double words as the Cortex
     * M55 can issue 64-bit data operations. Adjust in/out addresses as the
     * data op goes, keep len updated manually.
     */
    "_asm_memcpy_doublewise:\n"
    "lsrs    r3, r2, #3\n"
    "beq    _asm_memcpy_bytewise\n"
    "_asm_memcpy_doublewise_loop:\n"
    "ldmia   r1!, {r4-r5}\n"
    "stmia   r0!, {r4-r5}\n"
    "subs    r3, r3, #1\n"
    "bne     _asm_memcpy_doublewise_loop\n"
    /*
     * Bytewise copy: use len as index to adjusted in/out addresses
     * Copy backwards since that's the most efficient.
     */
    "_asm_memcpy_bytewise:\n"
    "pop     {r4-r11}\n"
    "_asm_memcpy_bytewise_nopop:\n"
    "ands    r2, r2, #7\n"
    "beq     _asm_memcpy_end\n"
    "_asm_memcpy_bytewise_loop:\n"
    "subs    r2, r2, #1\n"
    "ldrb    r3, [r1, r2]\n"
    "strb    r3, [r0, r2]\n"
    "bne     _asm_memcpy_bytewise_loop\n"
    "_asm_memcpy_end:\n"
    "pop     {r0, pc}\n"
    /*
     * Copy with realignment. Use large access and shift/rotate operations to
     * speed up copying versus plain bytewise copy. Strategy:
     * - Load eight words at a time (we've front-aligned, so input is loadable
     *   with ldm)
     * - Write out part of the first word that gets us to alignment on the
     *   output
     * - Shift bytes down across all words
     * - Write out full words using STMIA
     * - Write out leftover part of the last word using STR
     * - When copying 8 words at a time is not possible anymore,
     *   copy 2 words at a time.
     */
    /* Go to the correct unrolled loop. */
    "_asm_memcpy_with_realign:\n"
    "cmp     r12, #2\n"
    "bgt     _asm_memcpy_octawise_shift_1\n"
    "blt.w   _asm_memcpy_octawise_shift_3\n"

    "_asm_memcpy_octawise_shift_2:\n"
    "lsrs    r3, r2, #5\n"
    "beq     _asm_memcpy_doublewise_shift_2\n"
    "_asm_memcpy_octawise_shift_2_loop:\n"
    "ldmia   r1!, {r4-r11}\n"
    "strh    r4, [r0], #2\n"
    "lsr     r4, r4, #16\n"
    "orr     r4, r4, r5, lsl #16\n"
    "lsr     r5, r5, #16\n"
    "orr     r5, r5, r6, lsl #16\n"
    "lsr     r6, r6, #16\n"
    "orr     r6, r6, r7, lsl #16\n"
    "lsr     r7, r7, #16\n"
    "orr     r7, r7, r8, lsl #16\n"
    "lsr     r8, r8, #16\n"
    "orr     r8, r8, r9, lsl #16\n"
    "lsr     r9, r9, #16\n"
    "orr     r9, r9, r10, lsl #16\n"
    "lsr     r10, r10, #16\n"
    "orr     r10, r10, r11, lsl #16\n"
    "lsr     r11, r11, #16\n"
    "strh    r11, [r0, #28]\n"
    "stmia   r0!, {r4-r10}\n"
    "subs    r3, r3, #1\n"
    "add     r0, r0, r12\n"
    "bne     _asm_memcpy_octawise_shift_2_loop\n"
    "and     r2, r2, #31\n"

    "_asm_memcpy_doublewise_shift_2:\n"
    "lsrs    r3, r2, #3\n"
    "beq     _asm_memcpy_bytewise\n"
    "_asm_memcpy_doublewise_shift_2_loop:\n"
    "ldmia   r1!, {r4-r5}\n"
    "strh    r4, [r0], #2\n"
    "lsr     r4, r4, #16\n"
    "orr     r4, r4, r5, lsl #16\n"
    "lsr     r5, r5, #16\n"
    "strh    r5, [r0, #4]\n"
    "str     r4, [r0], #6\n"
    "subs    r3, r3, #1\n"
    "bne     _asm_memcpy_doublewise_shift_2_loop\n"
    "b       _asm_memcpy_bytewise\n"

    "_asm_memcpy_octawise_shift_1:\n"
    "lsrs    r3, r2, #5\n"
    "beq     _asm_memcpy_doublewise_shift_1\n"
    "_asm_memcpy_octawise_shift_1_loop:\n"
    "ldmia   r1!, {r4-r11}\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "orr     r4, r4, r5, lsl #24\n"
    "lsr     r5, r5, #8\n"
    "orr     r5, r5, r6, lsl #24\n"
    "lsr     r6, r6, #8\n"
    "orr     r6, r6, r7, lsl #24\n"
    "lsr     r7, r7, #8\n"
    "orr     r7, r7, r8, lsl #24\n"
    "lsr     r8, r8, #8\n"
    "orr     r8, r8, r9, lsl #24\n"
    "lsr     r9, r9, #8\n"
    "orr     r9, r9, r10, lsl #24\n"
    "lsr     r10, r10, #8\n"
    "orr     r10, r10, r11, lsl #24\n"
    "lsr     r11, r11, #8\n"
    "strb    r11, [r0, #28]\n"
    "lsr     r11, r11, #8\n"
    "strb    r11, [r0, #29]\n"
    "lsr     r11, r11, #8\n"
    "strb    r11, [r0, #30]\n"
    "stmia   r0!, {r4-r10}\n"
    "subs    r3, r3, #1\n"
    "add     r0, r0, r12\n"
    "bne     _asm_memcpy_octawise_shift_1_loop\n"
    "and     r2, r2, #31\n"

    "_asm_memcpy_doublewise_shift_1:\n"
    "lsrs    r3, r2, #3\n"
    "beq.w   _asm_memcpy_bytewise\n"
    "_asm_memcpy_doublewise_shift_1_loop:\n"
    "ldmia   r1!, {r4-r5}\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "orr     r4, r4, r5, lsl #24\n"
    "lsr     r5, r5, #8\n"
    "strb    r5, [r0, #4]\n"
    "lsr     r5, r5, #8\n"
    "strb    r5, [r0, #5]\n"
    "lsr     r5, r5, #8\n"
    "strb    r5, [r0, #6]\n"
    "str     r4, [r0], #7\n"
    "subs    r3, r3, #1\n"
    "bne     _asm_memcpy_doublewise_shift_1_loop\n"
    "b       _asm_memcpy_bytewise\n"

    "_asm_memcpy_octawise_shift_3:\n"
    "lsrs    r3, r2, #5\n"
    "beq     _asm_memcpy_doublewise_shift_3\n"
    "_asm_memcpy_octawise_shift_3_loop:\n"
    "ldmia   r1!, {r4-r11}\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "orr     r4, r4, r5, lsl #8\n"
    "lsr     r5, r5, #24\n"
    "orr     r5, r5, r6, lsl #8\n"
    "lsr     r6, r6, #24\n"
    "orr     r6, r6, r7, lsl #8\n"
    "lsr     r7, r7, #24\n"
    "orr     r7, r7, r8, lsl #8\n"
    "lsr     r8, r8, #24\n"
    "orr     r8, r8, r9, lsl #8\n"
    "lsr     r9, r9, #24\n"
    "orr     r9, r9, r10, lsl #8\n"
    "lsr     r10, r10, #24\n"
    "orr     r10, r10, r11, lsl #8\n"
    "lsr     r11, r11, #24\n"
    "strb    r11, [r0, #28]\n"
    "stmia   r0!, {r4-r10}\n"
    "subs    r3, r3, #1\n"
    "add     r0, r0, r12\n"
    "bne     _asm_memcpy_octawise_shift_3_loop\n"
    "and     r2, r2, #31\n"

    "_asm_memcpy_doublewise_shift_3:\n"
    "lsrs    r3, r2, #3\n"
    "beq.w   _asm_memcpy_bytewise\n"
    "_asm_memcpy_doublewise_shift_3_loop:\n"
    "ldmia   r1!, {r4-r5}\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "strb    r4, [r0], #1\n"
    "lsr     r4, r4, #8\n"
    "orr     r4, r4, r5, lsl #8\n"
    "lsr     r5, r5, #24\n"
    "strb    r5, [r0, #4]\n"
    "str     r4, [r0], #5\n"
    "subs    r3, r3, #1\n"
    "bne     _asm_memcpy_doublewise_shift_3_loop\n"
    "b       _asm_memcpy_bytewise\n"
#if defined(__clang__)
    :
    :
    : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "lr", "memory"
    );
  // LLVM: No return statement allowed in naked functions
#else
    // GCC and IAR: Can use parameter constraints
    :[r0] "+r" (dst), [r1] "+r" (src), [r2] "+r" (len)
    );
  return dst;
#endif
}

ATTR_EXT_VIS __attribute__((alias("sl_memcpy")))
void* STD_LIB_WRAPPER_MEMCPY(void *dst, const void *src, size_t len);

#if defined(__IAR_SYSTEMS_ICC__)
ATTR_EXT_VIS __attribute__((alias("sl_memcpy")))
void* STD_LIB_WRAPPER_MEMCPY_SIZE(void *dst, const void *src, size_t len);
#endif
