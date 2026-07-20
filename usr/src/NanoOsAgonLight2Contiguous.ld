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
/* Loads at RAM address 0x60000, inside the external SRAM region */
/* (0x40000-0xC0000) but well clear of the regular overlays' 0xFFE000. */

MEMORY
{
    /* Overlay RAM region - sized for the 32KB contiguous filesystem cap */
    OVERLAY_RAM (rwx) : ORIGIN = 0x60000, LENGTH = 32K
}

/* Entry point - not critical for overlays but good practice */
ENTRY(__start)

/*
 * Provide a default __start so that overlays without an explicit entry point
 * (library overlays) don't cause the linker to pull in crt0.o from libagon.a
 * trying to satisfy ENTRY's symbol search.  Overlays that define _start in C
 * (which the eZ80 ABI mangles to __start) override this via normal symbol
 * precedence.
 */
PROVIDE(__start = ORIGIN(OVERLAY_RAM));

SECTIONS
{
    /* Overlay header must be first - contains export table info */
    .overlay_header 0x60000 :
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
         * any other retained section.  This linker's --gc-sections marking
         * resolves the ENTRY(__start) root against the PROVIDE default
         * above instead of start.o's real definition, so without an
         * explicit KEEP here the actual __start (and everything it calls,
         * e.g. main) gets swept away, leaving only the export table.
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
        *(.debug*)
    }
}

