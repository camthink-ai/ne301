/***************************************************************************//**
 * @file
 * @brief Implementation of fast memset.
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
#error "The sl_memset component is not supported for this device."
#endif
#if defined(__clang__) || defined(__GNUC__)
// Wrapping a system function works by using the linker option '--wrap=symbol'.
// Any undefined reference to "symbol" will be resolved to "__wrap_symbol".
// Any undefined reference to "__real_symbol" will be resolved to "symbol".
#define STD_LIB_WRAPPER_MEMSET  __wrap_memset

// Coverage/profile instrumentation (-fprofile-arcs) injects counter-increment
// code at function entry that clobbers caller-visible registers before our
// inline assembly runs. Because this is a naked function and the short-data
// path does not push/pop callee-saved registers, the corrupted values leak
// back to the caller and violate AAPCS. Prevent that by opting this hot
// function out of profile instrumentation.
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
#define STD_LIB_WRAPPER_MEMSET        $Sub$$__aeabi_memset
#define STD_LIB_WRAPPER_MEMSET_SIZE   $Sub$$__iar_small_memset

// Since IAR does not use LTO, resolve the attribute as nothing.
#define ATTR_EXT_VIS

// IAR does not support gcov-style instrumentation in this configuration.
#define ATTR_NO_PROFILE

#else
#error "Unsupported compiler for standard C memory functions retarget."
#endif

/*******************************************************************************
 * @brief   Fast memset using ARM assembly. Fills a number of bytes in the
 *          destination with a constant byte.
 *
 * @param[out] dst  Destination address.
 * @param[in]  ch   Unsigned character used to fill.
 * @param[in]  len  Number of bytes to fill.
 *
 * @return  Destination address.
 *
 * @details Register assignment:
 *  r0:     dst (output pointer)
 *  r1:     ch (character used to fill) r2 on IAR
 *  r2:     len (number of bytes to fill) r1 on IAR
 *  r3-r9:  copies of r1
 *  r12:    number of chunks to fill
 ******************************************************************************/
#if defined(__GNUC__)
ATTR_NO_PROFILE __NO_PROLOGUE void* sl_memset(void *dst, int ch, size_t len)
{
  __ASM volatile(
    /* Function entry - save retval */
    "push    {r0, lr}\n"
    /* Check size, do bytewise immediately for small data (< 8 bytes) */
    "cmp     r2, #8\n"
    "bmi     _asm_memset_bytewise\n"
    /* Check alignment, do multiwordwise set if already aligned */
    "ands    r3, r0, #3\n"
    "rsb     r3, r3, #4\n"
    "beq     _asm_memset_octawise\n"
    "sub     r2, r2, r3\n"
    "_asm_memset_align_loop:\n"
    "subs    r3, r3, #1\n"
    "strb    r1, [r0], #1\n"
    "bne     _asm_memset_align_loop\n"
    /*
     * Octa-wordwise memset: use stmia with eight words. Adjust address
     * as the data op goes, keep len updated manually.
     */
    "_asm_memset_octawise:\n"
    /* Duplicate single byte across all four bytes in the register */
    "and     r1, r1, #255\n"
    "orr     r1, r1, r1, lsl #8\n"
    "orr     r1, r1, r1, lsl #16\n"
    /* Duplicate register across all registers used during stm */
    "mov     r3, r1\n"
    "lsrs    r12, r2, #5\n"
    "beq     _asm_memset_doublewise\n"
    "push    {r4-r9}\n"
    "mov     r4, r1\n"
    "mov     r5, r1\n"
    "mov     r6, r1\n"
    "mov     r7, r1\n"
    "mov     r8, r1\n"
    "mov     r9, r1\n"
    "_asm_memset_octawise_loop:\n"
    "subs    r12, r12, #1\n"
    "stmia   r0!, {r1, r3, r4, r5, r6, r7, r8, r9}\n"
    "bne     _asm_memset_octawise_loop\n"
    "pop     {r4-r9}\n"
    "and     r2, r2, #31\n"
    /*
     * Double-wordwise memset: use stmia with double words as the Cortex M55
     * can issue 64-bit data operations. Adjust address as the data op goes,
     * keep len updated manually.
     */
    "_asm_memset_doublewise:\n"
    "lsrs    r12, r2, #3\n"
    "beq     _asm_memset_bytewise\n"
    "_asm_memset_doublewise_loop:\n"
    "subs    r12, r12, #1\n"
    "stmia   r0!, {r1, r3}\n"
    "bne     _asm_memset_doublewise_loop\n"
    /*
     * Bytewise memset: use len as index to adjusted in/out addresses
     * Go backwards since that's the most efficient...
     */
    "_asm_memset_bytewise:\n"
    "ands    r2, r2, #7\n"
    "beq     _asm_memset_end\n"
    "_asm_memset_bytewise_loop:\n"
    "subs    r2, r2, #1\n"
    "strb    r1, [r0, r2]\n"
    "bne     _asm_memset_bytewise_loop\n"
    "_asm_memset_end:\n"
    "pop     {r0, pc}\n"
#if defined(__clang__)
    :
    :
    : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "lr", "memory"
    );
  // LLVM: No return statement allowed in naked functions
#else
    // GCC: Can use parameter constraints
    :[r0] "+r" (dst), [r1] "+r" (ch), [r2] "+r" (len)
    );
  return dst;
#endif
}

#else
ATTR_NO_PROFILE __NO_PROLOGUE void* sl_memset(void *dst, size_t len, int ch)
{
  __ASM volatile(
    /* Function entry - save retval */
    "push    {r0, lr}\n"
    /* Check size, do bytewise immediately for small data (< 8 bytes) */
    "cmp     r1, #8\n"
    "bmi     _asm_memset_bytewise\n"
    /* Check alignment, do multiwordwise set if already aligned */
    "ands    r3, r0, #3\n"
    "rsb     r3, r3, #4\n"
    "beq     _asm_memset_octawise\n"
    "sub     r1, r1, r3\n"
    "_asm_memset_align_loop:\n"
    "subs    r3, r3, #1\n"
    "strb    r2, [r0], #1\n"
    "bne     _asm_memset_align_loop\n"
    /*
     * Octa-wordwise memset: use stmia with eight words. Adjust address
     * as the data op goes, keep len updated manually.
     */
    "_asm_memset_octawise:\n"
    /* Duplicate single byte across all four bytes in the register */
    "and     r2, r2, #255\n"
    "orr     r2, r2, r2, lsl #8\n"
    "orr     r2, r2, r2, lsl #16\n"
    /* Duplicate register across all registers used during stm */
    "mov     r3, r2\n"
    "lsrs    r12, r1, #5\n"
    "beq     _asm_memset_doublewise\n"
    "push    {r4-r9}\n"
    "mov     r4, r2\n"
    "mov     r5, r2\n"
    "mov     r6, r2\n"
    "mov     r7, r2\n"
    "mov     r8, r2\n"
    "mov     r9, r2\n"
    "_asm_memset_octawise_loop:\n"
    "subs    r12, r12, #1\n"
    "stmia   r0!, {r2, r3, r4, r5, r6, r7, r8, r9}\n"
    "bne     _asm_memset_octawise_loop\n"
    "pop     {r4-r9}\n"
    "and     r1, r1, #31\n"
    /*
     * Double-wordwise memset: use stmia with double words as the Cortex M55
     * can issue 64-bit data operations. Adjust address as the data op goes,
     * keep len updated manually.
     */
    "_asm_memset_doublewise:\n"
    "lsrs    r12, r1, #3\n"
    "beq     _asm_memset_bytewise\n"
    "_asm_memset_doublewise_loop:\n"
    "subs    r12, r12, #1\n"
    "stmia   r0!, {r2, r3}\n"
    "bne     _asm_memset_doublewise_loop\n"
    /*
     * Bytewise memset: use len as index to adjusted in/out addresses
     * Go backwards since that's the most efficient...
     */
    "_asm_memset_bytewise:\n"
    "ands    r1, r1, #7\n"
    "beq     _asm_memset_end\n"
    "_asm_memset_bytewise_loop:\n"
    "subs    r1, r1, #1\n"
    "strb    r2, [r0, r1]\n"
    "bne     _asm_memset_bytewise_loop\n"
    "_asm_memset_end:\n"
    "pop     {r0, pc}\n"
    :[r0] "+r" (dst), [r1] "+r" (len), [r2] "+r" (ch)
    );
  return dst;
}
#endif

#if defined(__IAR_SYSTEMS_ICC__)
ATTR_EXT_VIS __attribute__((alias("sl_memset")))
void* STD_LIB_WRAPPER_MEMSET(void *dst, size_t len, int ch);
ATTR_EXT_VIS __attribute__((alias("sl_memset")))
void* STD_LIB_WRAPPER_MEMSET_SIZE(void *dst, size_t len, int ch);
#else
ATTR_EXT_VIS __attribute__((alias("sl_memset")))
void* STD_LIB_WRAPPER_MEMSET(void *dst, int ch, size_t len);
#endif
