/*
 * AgonLight2.ld — Linker script for NanoOs on the Agon Light 2 (eZ80F92).
 *
 * NanoOs is the primary firmware on this target: it boots from the eZ80
 * reset vector at address 0x000000 and replaces MOS entirely.  It is not a
 * MOS user program and carries no MOS load header.
 *
 * This is NOT yet a complete flash-boot memory model.  It places the entry
 * point at 0x000000 and treats .text/.data/.rodata/.bss as one flat,
 * directly-executable region.  Real bring-up needs .text to execute in place
 * from flash while .data/.bss live in RAM, which means .data needs a separate
 * load address (LMA) in flash and a copy-to-RAM step in Boot.asm before the
 * BSS-zero loop — the same pattern the SAMD21 ports already use.  The heap and
 * overlay addresses below are still the values carried over from the earlier
 * MOS-program design in HalAgonLight2.c and are not yet re-derived for this
 * layout.  That is follow-up work.
 *
 * Symbol contracts with Boot.asm:
 *   __bss_start  — start of BSS (used in BSS-zero loop)
 *   __bss_size   — byte count of BSS (used in BSS-zero loop)
 *
 * Symbol contracts with HalAgonLight2.c:
 *   __bss_end__  — end of BSS (checked against OVERLAY_ADDRESS)
 */

ENTRY(_start)

MEMORY
{
  SRAM (rwx) : ORIGIN = 0x000000, LENGTH = 0x080000
}

SECTIONS
{
  /* ---- Code ---------------------------------------------------------------- */
  .text :
  {
    KEEP(*(.text.boot))       /* Boot.asm _start must be the very first bytes */

    /* eZ80F92 mode 2 interrupt vector table and the code it points at.
     * The CPU forms the vector address as { I[7:0], IVECT[7:0] } and jumps to
     * { 00h, entry }, so both the 256-byte table and every handler it names
     * must sit on a 256-byte boundary within the first 64 KB.  Kept here,
     * right after the boot code, so they land low.  See
     * src/hal/AgonLight2/Interrupts.asm.  (This flat scaffold keeps .ivt in
     * the image as-is; a real flash build will need it copied to / fixed up
     * in low RAM.) */
    . = ALIGN(0x100);
    __ivt_start = .;
    KEEP(*(.ivt))
    KEEP(*(.text.ivt))
    __ivt_end = .;

    *(.text .text.*)
    . = ALIGN(4);
  } > SRAM

  ASSERT(__ivt_end <= 0x10000,
    "Interrupt vector table + .text.ivt overflow the first 64 KB")

  /* ---- Initialised data ------------------------------------------------- */
  .data :
  {
    __data_start__ = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end__ = .;
  } > SRAM

  /* ---- Read-only data (strings, const tables) ------------------------------ */
  .rodata :
  {
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > SRAM

  /* ---- Zero-initialised data -------------------------------------------- */
  .bss :
  {
    . = ALIGN(4);
    __bss_start = .;          /* referenced by Boot.asm */
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;          /* referenced by HalAgonLight2.c */
    __bss_size  = ABSOLUTE(__bss_end__ - __bss_start);   /* referenced by Boot.asm */
  } > SRAM

  /* Heap boundaries carried over from the earlier MOS-program design.
   * Not yet re-derived for the flash-boot memory map. */
  ___heapbot = __bss_end__;
  ___heaptop = 0x080000;

  /* Discard C++ unwinding tables — bare-metal, no exceptions */
  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.ARM.*)
  }
}
