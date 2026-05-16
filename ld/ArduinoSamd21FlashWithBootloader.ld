/*
  Based on the Adafruit ItsyBitsy M0 Express linker script.
  Modified for NanoOS: sketch .rodata (mainly strings) is placed at the end
  of flash so it can be stripped from the binary, while Arduino core,
  library, and toolchain C library .rodata stays in .text where the CPU
  can access it at runtime.

  To strip sketch strings from the flash binary:
    arm-none-eabi-objcopy -O binary -R .rodata NanoOs.elf NanoOs_stripped.bin

  The full ELF retains .rodata for host-side string address resolution.

  Original copyright:
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  LGPL v2.1 or later.
*/

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000+0x2000, LENGTH = 0x00040000-0x2000 /* First 8KB used by bootloader */
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00008000
}

ENTRY(Reset_Handler)

SECTIONS
{
	.text :
	{
		__text_start__ = .;

		KEEP(*(.sketch_boot))

		. = ALIGN(0x2000);
		KEEP(*(.isr_vector))
		*(.text*)

		KEEP(*(.init))
		KEEP(*(.fini))

		/* .ctors */
		*crtbegin.o(.ctors)
		*crtbegin?.o(.ctors)
		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
		*(SORT(.ctors.*))
		*(.ctors)

		/* .dtors */
 		*crtbegin.o(.dtors)
 		*crtbegin?.o(.dtors)
 		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
 		*(SORT(.dtors.*))
 		*(.dtors)

		/*
		 * Keep Arduino core, library, and toolchain C library .rodata
		 * here in .text.  These contain runtime-essential const data
		 * (Serial, Uart, sercomData, USB descriptors, _ctype_ table,
		 * printf float tables, locale data, stdio FILE structs, etc.)
		 * that the CPU must be able to read from flash at all times.
		 *
		 * The linker claims each input section on first match, so
		 * anything matched here will NOT appear in the strippable
		 * .rodata section below.
		 *
		 * If you add new Arduino libraries, add their .rodata here.
		 */

		/* Arduino core and libraries */
		*/core.a:(.rodata*)
		*/SPI.cpp.o(.rodata*)
		*/Adafruit_ZeroDMA.cpp.o(.rodata*)
		*/variant.cpp.o(.rodata*)

		/* Toolchain C library, math library, and runtime support */
		*/libc.a:(.rodata*)
		*/libc_nano.a:(.rodata*)
		*/libm.a:(.rodata*)
		*/libg.a:(.rodata*)
		*/libg_nano.a:(.rodata*)
		*/libgcc.a:(.rodata*)
		*/libnosys.a:(.rodata*)

		KEEP(*(.eh_frame*))
	} > FLASH

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > FLASH

	__exidx_start = .;
	.ARM.exidx :
	{
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
	} > FLASH
	__exidx_end = .;

	__etext = .;

	.data : AT (__etext)
	{
		__data_start__ = .;
		*(vtable)
		*(.data*)

		. = ALIGN(4);
		/* preinit data */
		PROVIDE_HIDDEN (__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN (__preinit_array_end = .);

		. = ALIGN(4);
		/* init data */
		PROVIDE_HIDDEN (__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		PROVIDE_HIDDEN (__init_array_end = .);


		. = ALIGN(4);
		/* finit data */
		PROVIDE_HIDDEN (__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array))
		PROVIDE_HIDDEN (__fini_array_end = .);

		KEEP(*(.jcr*))
		. = ALIGN(16);
		/* All data end */
		__data_end__ = .;

	} > RAM

	/*
	 * Sketch .rodata — placed after .data's LMA (load image) in flash.
	 *
	 * By this point the Arduino core, library, and toolchain .rodata has
	 * been claimed by .text above, so only sketch object .rodata lands
	 * here.  This is predominantly string literals (__func__, log
	 * messages, format strings, etc.) and any const data from your
	 * NanoOS source files.
	 *
	 * Strippable via:  objcopy -O binary -R .rodata NanoOs.elf NanoOs_stripped.bin
	 *
	 * IMPORTANT: if any of YOUR code has const data the CPU dereferences
	 * at runtime (function pointer tables, const struct instances, lookup
	 * tables), either:
	 *   (a) remove the const qualifier (moves it to .data / RAM), or
	 *   (b) tag it with __attribute__((section(".text"))) to keep it
	 *       in the non-strippable code section, e.g.:
	 *
	 *       #define KEEP_IN_FLASH __attribute__((section(".text")))
	 *       KEEP_IN_FLASH const CommandHandler handlers[] = { ... };
	 */
	.rodata (__etext + SIZEOF(.data)) :
	{
		. = ALIGN(4);
		__rodata_start__ = .;
		*(.rodata*)
		. = ALIGN(4);
		__rodata_end__ = .;
	} > FLASH

	.bss :
	{
		. = ALIGN(4);
		__bss_start__ = .;
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end__ = .;
	} > RAM

	.heap (COPY):
	{
		__end__ = .;
		PROVIDE(end = .);
		*(.heap*)
		__HeapLimit = .;
	} > RAM

	/* .stack_dummy section doesn't contains any symbols. It is only
	 * used for linker to calculate size of stack sections, and assign
	 * values to stack symbols later */
	.stack_dummy (COPY):
	{
		*(.stack*)
	} > RAM

	/* Set stack top to end of RAM, and stack limit move down by
	 * size of stack_dummy section */
	__StackTop = ORIGIN(RAM) + LENGTH(RAM);
	__StackLimit = __StackTop - SIZEOF(.stack_dummy);
	PROVIDE(__stack = __StackTop);

	__ram_end__ = ORIGIN(RAM) + LENGTH(RAM) -1 ;

	/* Check if data + heap + stack exceeds RAM limit */
	ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")
}
