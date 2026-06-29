; boot.asm — NanoOs startup for Agon Light 2 as a MOS user program.
;
; MOS loads the binary into external SRAM at 0x040000 and jumps to the
; first byte.  The CPU is already in ADL (24-bit) mode when we arrive;
; no mode-switch jump is required.
;
; Assembled with ez80-none-elf-clang (LLVM integrated assembler) via
;   clang --target=ez80-none-elf -x assembler-with-cpp
;
; Provides:  _start  — program entry point (named in AgonLight2.ld)
; Requires:  main    — C  int main(void)  in AgonLight2/main.c
;            __bss_start, __bss_size — absolute symbols from linker script

    .section .text.boot,"ax"
    .assume ADL=1
    .globl  _start

_start:
    DI                          ; disable maskable interrupts while setting up

    ; Point the stack at the top of external SRAM (4-byte aligned).
    ; External SRAM: 0x040000–0x0BFFFF (512 KB).
    LD  SP, 0x0BFFFC

    ; Zero BSS.  Skip the loop when __bss_size == 0.
    LD  HL, __bss_start
    LD  BC, __bss_size
    LD  A, B
    OR  C
    JR  Z, .bss_done
    XOR A
.bss_loop:
    LD  (HL), A
    INC HL
    DEC BC
    LD  A, B
    OR  C
    JR  NZ, .bss_loop
.bss_done:

    ; Jump into the C entry point.
    CALL _main

    ; NanoOs never returns from main().  Halt the CPU if it somehow does.
.hang:
    HALT
    JR  .hang

    .extern __bss_start
    .extern __bss_size
    .extern _main
