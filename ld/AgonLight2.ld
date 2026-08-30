/*
 * AgonLight2.ld — Linker script for NanoOs on the Agon Light 2 (eZ80F92).
 *
 * NanoOs is the primary firmware on this target: it boots from the eZ80 reset
 * vector at address 0x000000 and replaces MOS entirely.  It is not a MOS user
 * program and carries no MOS load header.
 *
 * Memory model
 * ------------
 *   FLASH   0x000000..0x01FFFF   128 KB on-chip flash.  Holds .text (the boot
 *                                code and the mode-2 interrupt vector block
 *                                included), the .data load image, and .rodata.
 *                                Executed / read in place; read-only at runtime.
 *   ext RAM 0x040000..0x0BFFFF   512 KB external SRAM:
 *            0x040000..0x047FFF  32 KB  contiguous filesystem image window
 *            0x048000..0x048FFF  4 KB   .bss then .data  (see HalAgonLight2.c
 *                                       DATA_BSS_REGION_ADDRESS / _SIZE)
 *            0x049000            8-byte data/bss integrity canary, then the heap
 *            ..0x0BFFFF          heap (grows up) toward the stack at 0x0C0000
 *   int RAM 0xFFE000..0xFFFFFF   8 KB on-chip SRAM, mapped by Boot.asm; the
 *                                overlay window (OVERLAY_ADDRESS).
 *
 * .data has its VMA in external SRAM and its LMA in flash immediately after
 * .text; boot/AgonLight2/Boot.asm copies it across at startup, before it zeroes
 * .bss.  .bss is emitted *before* .data so that overrunning the 4 KB
 * reservation is caught both here (ASSERT below) and at run time: Boot.asm
 * stamps a known pattern at __data_bss_limit before the copy and
 * halAgonLight2Init() rechecks it as its final step.
 *
 * .rodata is placed last in flash so `objcopy -O binary -R .rodata` truncates
 * the shipped image rather than leaving a zero-filled hole.
 *
 * Symbol contracts with Boot.asm:
 *   __bss_start, __bss_size                      — BSS-zero loop
 *   __data_start__, __data_size, __data_load__   — .data copy loop
 *   __data_bss_limit                             — address of the integrity canary
 * Symbol contracts with HalAgonLight2.c:
 *   __bss_end__                                  — checked against the overlay window
 */

ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x000000, LENGTH = 0x020000   /* 128 KB on-chip flash   */
  RAM   (rwx) : ORIGIN = 0x048000, LENGTH = 0x078000   /* ext SRAM above the FS window, up to 0x0C0000 */
}

SECTIONS
{
  /* 36 KB into external SRAM: one byte past the .bss/.data reservation.
   * Keep in sync with DATA_BSS_CANARY_ADDRESS in src/hal/HalAgonLight2.c. */
  __data_bss_limit = 0x049000;

  /* ---- Code + read-only data: in flash, executed / read in place --------- */
  .text :
  {
    KEEP(*(.text.boot))       /* Boot.asm _start must be the very first bytes */

    /* eZ80F92 mode 2 interrupt vector table and the code it points at.
     * The CPU forms the vector address as { I[7:0], IVECT[7:0] } and jumps to
     * { 00h, entry }, so both the 256-byte table and every handler it names
     * must sit on a 256-byte boundary within the first 64 KB.  Kept here,
     * right after the boot code, so they land low.  See
     * src/hal/AgonLight2/Interrupts.asm. */
    . = ALIGN(0x100);
    __ivt_start = .;
    KEEP(*(.ivt))
    KEEP(*(.text.ivt))
    __ivt_end = .;

    *(.text .text.*)
    . = ALIGN(4);
    __etext = .;
  } > FLASH

  ASSERT(__ivt_end <= 0x10000,
    "Interrupt vector table + .text.ivt overflow the first 64 KB")

  /* ---- Zero-initialised data: external SRAM, emitted before .data -------- */
  .bss :
  {
    . = ALIGN(4);
    __bss_start = .;          /* referenced by Boot.asm */
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;          /* referenced by HalAgonLight2.c */
    __bss_size  = ABSOLUTE(__bss_end__ - __bss_start);   /* referenced by Boot.asm */
  } > RAM

  /* ---- Initialised data: VMA in external SRAM, LMA in flash after .text -- */
  .data : AT (__etext)
  {
    . = ALIGN(4);
    __data_start__ = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end__ = .;
    __data_size  = ABSOLUTE(__data_end__ - __data_start__);   /* referenced by Boot.asm */
  } > RAM
  __data_load__ = LOADADDR(.data);   /* referenced by Boot.asm */

  /* ---- Read-only data: flash, right after the .data load image ----------- */
  .rodata (__etext + SIZEOF(.data)) :
  {
    *(.rodata .rodata.*)
    . = ALIGN(4);
    __flash_end = .;
  } > FLASH

  ASSERT(__data_end__ <= __data_bss_limit,
    ".bss + .data exceed their 4 KB external-RAM reservation (canary at 0x049000)")
  ASSERT(__flash_end <= 0x020000,
    "flash image (.text + .data load image + .rodata) overflows the 128 KB on-chip flash")

  /* libagon's _sbrk / malloc arena.  NanoOs uses its own MemoryManager and the
   * HAL bans malloc, so this exists only to satisfy libagon's link references;
   * keep it inside the .bss/.data reservation gap, capped below the integrity
   * canary so a stray libagon allocation can never reach the kernel heap. */
  ___heapbot = __data_end__;
  ___heaptop = __data_bss_limit;

  /* Discard C++ unwinding tables — bare-metal, no exceptions */
  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.ARM.*)
  }
}
