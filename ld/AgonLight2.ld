/*
 * AgonLight2.ld — Linker script for NanoOs on the Agon Light 2 (eZ80F92)
 *                 running as a MOS user program.
 *
 * Memory map (eZ80F92 as used on the Agon Light 2):
 *   Internal Flash : 0x000000–0x01FFFF  128 KB  (MOS + VDP firmware)
 *   External SRAM  : 0x040000–0x0BFFFF  512 KB  (MOS loads user programs here)
 *   Internal SRAM  : 0x0C0000–0x0CFFFF   64 KB
 *
 * MOS copies the flat binary from the SD card into external SRAM at 0x040000
 * and jumps to the first byte.  The CPU is already in ADL (24-bit) mode.
 *
 * NanoOs address-space layout (within the 512 KB external SRAM window):
 *   0x040000  .text             — code                      (grows up)
 *   .         .data             — initialised data
 *   .         .rodata           — read-only data / strings  (end of binary — strippable)
 *   .         .bss              — zero-initialised data
 *   0x080000  overlay region    — 64 KB (OVERLAY_SIZE in HalAgonLight2.c)
 *   0x090000  heap              — remainder of SRAM (grows up)
 *   0x0BFFFC  stack             — stack pointer set by boot.asm (grows down)
 *
 * .rodata is placed after .data so that objcopy -R .rodata truncates the
 * binary rather than leaving a same-size zero-filled gap.  The stripped
 * binary is shorter by exactly SIZEOF(.rodata).
 *
 * WARNING: any const object the CPU dereferences at runtime (function
 * pointer tables, const struct instances, lookup tables) must not land in
 * .rodata.  Remove its const qualifier (goes to .data) or force it into
 * .text with __attribute__((section(".text"))).
 *
 * Symbol contracts with boot.asm:
 *   __bss_start  — start of BSS (used in BSS-zero loop)
 *   __bss_size   — byte count of BSS (used in BSS-zero loop)
 *
 * Symbol contracts with HalAgonLight2.c:
 *   __bss_end__  — end of BSS (checked against OVERLAY_ADDRESS)
 */

ENTRY(_start)

MEMORY
{
  SRAM (rwx) : ORIGIN = 0x040000, LENGTH = 0x080000   /* 512 KB external SRAM */
}

SECTIONS
{
  /* ---- Code ---------------------------------------------------------------- */
  .text :
  {
    KEEP(*(.text.boot))       /* boot.asm _start must be the very first bytes */
    *(.text .text.*)
    . = ALIGN(4);
  } > SRAM

  /* ---- Initialised data ------------------------------------------------- */
  .data :
  {
    __data_start__ = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end__ = .;
  } > SRAM

  /* ---- Read-only data (strings, const tables) ------------------------------ */
  /* Placed AFTER .data so that objcopy -R .rodata truncates the binary rather
   * than leaving a same-size zero-filled gap.  The stripped binary is smaller
   * by exactly SIZEOF(.rodata) bytes. */
  .rodata :
  {
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > SRAM

  /* ---- Zero-initialised data -------------------------------------------- */
  .bss :
  {
    . = ALIGN(4);
    __bss_start = .;          /* referenced by boot.asm */
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;          /* referenced by HalAgonLight2.c */
    __bss_size  = __bss_end__ - __bss_start;   /* referenced by boot.asm */
  } > SRAM

  /* Heap boundaries for libagon's sbrk/malloc.
   * NanoOs places its overlay at 0x080000, so the malloc heap occupies
   * the gap between the end of BSS and the overlay region. */
  ___heapbot = __bss_end__;
  ___heaptop = 0x080000;

  /* Discard C++ unwinding tables — bare-metal, no exceptions */
  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.ARM.*)
  }
}
