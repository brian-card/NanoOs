/*
 * AgonLight2Standalone.ld — Linker script for the NanoOs "replaces MOS"
 *                            scaffold on the Agon Light 2 (eZ80F92).
 *
 * Built via: make -f AgonLight2Makefile STANDALONE=1
 *
 * This is NOT a complete standalone-boot memory model. It only moves the
 * entry point to the eZ80 reset vector (address 0x000000) and keeps the
 * rest of the current MOS-loadable layout as placeholder numbers (heap and
 * overlay addresses are copied from the MOS-loadable design in
 * HalAgonLight2.c and are not yet re-derived for a standalone target).
 *
 * Known gap: this script still treats .text/.data/.rodata/.bss as one flat,
 * directly-executable region, same as the MOS-loadable script. Real
 * standalone bring-up needs .text to execute in place from flash while
 * .data/.bss live in RAM, which means .data needs a separate load address
 * (LMA) in flash and a copy-to-RAM step in boot.asm before the BSS-zero
 * loop — the same pattern the SAMD21 ports already use. That is future
 * work; see boot/AgonLight2/boot.asm for the corresponding entry-point and
 * vector-stub half of this scaffold.
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
  SRAM (rwx) : ORIGIN = 0x000000, LENGTH = 0x080000
}

SECTIONS
{
  /* ---- Code ---------------------------------------------------------------- */
  .text :
  {
    KEEP(*(.text.boot))       /* boot.asm _start must be the very first bytes */

    /* eZ80F92 mode 2 interrupt vector table and the code it points at.
     * The CPU forms the vector address as { I[7:0], IVECT[7:0] } and jumps to
     * { 00h, entry }, so both the 256-byte table and every handler it names
     * must sit on a 256-byte boundary within the first 64 KB.  Kept here,
     * right after the boot code, so they land low.  See
     * src/hal/AgonLight2/Interrupts.asm.  (This scaffold keeps .ivt in the
     * flat image; a real flash build will need it copied to / fixed up in
     * low RAM.) */
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
    __bss_start = .;          /* referenced by boot.asm */
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;          /* referenced by HalAgonLight2.c */
    __bss_size  = ABSOLUTE(__bss_end__ - __bss_start);   /* referenced by boot.asm */
  } > SRAM

  /* Placeholder heap boundaries, copied from the MOS-loadable script.
   * Not yet re-derived for a standalone memory map. */
  ___heapbot = __bss_end__;
  ___heaptop = 0x080000;

  /* Discard C++ unwinding tables — bare-metal, no exceptions */
  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.ARM.*)
  }
}
