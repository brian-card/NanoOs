/**
 *
 *  @author            James Card
 *  @date              09.01.2025
 *
 *  @file              NanoOsAgonLight2Contiguous.ld
 *
 *  @brief             Linker script for the contiguous filesystem overlay
 *                     on the Zilog eZ80.  Loads at a different base address
 *                     than the regular per-slot overlays (0xFFE000) because,
 *                     unlike those, it stays permanently resident rather
 *                     than being swapped in and out, and so cannot share
 *                     their memory.  For use with ez80-none-elf-clang.
 *
 *  @copyright
 *                    Copyright (c) 2012-2025 James Card
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *                                 James Card
 *                          http://www.jamescard.org
 *
 */

/* Contiguous filesystem linker script for Zilog eZ80 */
/* Loads at RAM address 0x40000 - this MUST equal FILESYSTEM_DRIVER_ADDRESS in */
/* src/hal/HalAgonLight2.c, since the overlay is position-dependent (its       */
/* export table holds absolute pointers).  0x40000 is the base of external     */
/* SRAM; the 32K window ends at DATA_BSS_REGION_ADDRESS (0x48000), just below   */
/* NanoOs's own .bss/.data.  Clear of the regular per-slot overlays at 0xFFE000.*/

MEMORY
{
    /* Overlay RAM region - sized for the 32KB contiguous filesystem cap */
    OVERLAY_RAM (rwx) : ORIGIN = 0x40000, LENGTH = 32K
}

/*
 * No ENTRY(__start) here.  NanoOs never uses the ELF entry point - overlays
 * are objcopy'd to flat binaries and their real entry point is looked up by
 * name through the .overlay_header export table at runtime.  ENTRY() was
 * tried for documentation purposes only, but combined with --gc-sections it
 * makes this linker resolve __start against the PROVIDE default below
 * instead of a real definition from start.o, even when one exists and is
 * correctly kept (see the KEEP(*(.text.__start)) note below) - silently
 * pointing _start at the overlay's own header instead of its code.
 * Omitting ENTRY avoids that resolution path entirely.  (This overlay
 * currently exports "main" directly rather than going through start.c, so
 * it was never actually hit by the bug - fixed here anyway to remove the
 * landmine for any future contiguous overlay that does use start.c.)
 *
 * PROVIDE still supplies a default __start so that overlays without an
 * explicit entry point (library overlays, which never reference __start at
 * all) don't need one.  Overlays that define _start in C (which the eZ80
 * ABI mangles to __start) get the real definition via normal symbol
 * precedence, since nothing now forces resolution through ENTRY.
 */
PROVIDE(__start = ORIGIN(OVERLAY_RAM));

SECTIONS
{
    /* Overlay header must be first - contains export table info */
    .overlay_header 0x40000 :
    {
        KEEP(*(.overlay_header))
        . = ALIGN(4);
    } > OVERLAY_RAM

    /* Code section */
    .text :
    {
        /*
         * The only live reference to __start is a function pointer inside
         * the KEEP'd .overlay_header export table, not a direct call from
         * any other retained section.  That reference is enough on its own
         * for --gc-sections to keep __start reachable now that ENTRY() is
         * gone above (verified against the sibling per-slot overlay script,
         * NanoOsAgonLight2.ld), so this is just belt-and-suspenders against
         * a future ENTRY() reintroduction reopening that resolution bug.
         */
        KEEP(*(.text.__start))
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > OVERLAY_RAM

    /* Initialized data */
    .data :
    {
        *(.data)
        *(.data*)
        . = ALIGN(4);
    } > OVERLAY_RAM

    /* Uninitialized data */
    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > OVERLAY_RAM

    /* Symbols for overlay size calculation */
    __overlay_start__ = ORIGIN(OVERLAY_RAM);
    __overlay_end__ = .;
    __overlay_size__ = __overlay_end__ - __overlay_start__;

    /* Discard sections we don't need */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
    }
}

