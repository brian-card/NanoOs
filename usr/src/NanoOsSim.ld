/**
 * 
 *  @author            James Card
 *  @date              11.22.2025
 * 
 *  @file              NanoOsSim.ld
 * 
 *  @brief             Linker script for simulator NanoOs user-space
 *                     applications organized at base address 0x20001800.
 *                     For use with the host OS's gcc.
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

/* Overlay Linker Script for host-based simulator application */
/* Overlay loads at RAM address 0x20001800 */

MEMORY
{
    /* Overlay RAM region - adjust size as needed */
    OVERLAY_RAM (rwx) : ORIGIN = 0x20001800, LENGTH = 8K
}

/* Entry point - not critical for overlays but good practice */
ENTRY(_start)

SECTIONS
{
    /* Overlay header must be first - contains export table info */
    .overlay_header 0x20001800 :
    {
        KEEP(*(.overlay_header))
        . = ALIGN(8);
    } > OVERLAY_RAM

    /* Code section */
    .text :
    {
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(8);
    } > OVERLAY_RAM

    /* Initialized data */
    .data :
    {
        *(.data)
        *(.data*)
        . = ALIGN(8);
    } > OVERLAY_RAM

    /* Uninitialized data */
    .bss :
    {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    } > OVERLAY_RAM

    /* Symbols for overlay size calculation */
    __overlay_start__ = ORIGIN(OVERLAY_RAM);
    __overlay_end__ = .;
    __overlay_size__ = __overlay_end__ - __overlay_start__;

    /* Discard sections we don't need */
    /DISCARD/ :
    {
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
        *(.note*)
        *(.gnu.hash)
        *(.gnu.version*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.debug*)
    }
}

