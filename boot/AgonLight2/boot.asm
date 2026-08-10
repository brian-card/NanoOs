; boot.asm — NanoOs startup for Agon Light 2.
;
; Two build modes, selected by NANO_OS_AGON_LIGHT_2_STANDALONE (set via
; STANDALONE=1 on the make command line; see AgonLight2Makefile):
;
; MOS-loadable (default):
;   MOS loads the binary into external SRAM at 0x040000 and jumps to the
;   first byte. The CPU is already in ADL (24-bit) mode when we arrive; no
;   mode-switch jump is required. MOS (mos_execMode in mos.c) refuses to
;   run a binary unless it carries a header at offset 0x40 from the load
;   address: the bytes "MOS" at 0x40-0x42 and a mode byte at 0x44 (0 =
;   Z80/16-bit, 1 = ADL/24-bit). Without it, RUN/EXEC report "Invalid
;   executable". _start therefore jumps over a padded gap containing that
;   header before falling into the real startup code.
;
; Standalone (scaffold for eventually replacing MOS):
;   Entry is address 0x000000 instead of 0x040000 — the eZ80 reset vector.
;   No MOS header is emitted since there's no MOS to check it. The low
;   memory RST (0x08/0x10/0x18/0x20/0x28/0x30/0x38) and NMI (0x66) vector
;   slots are stubbed with a safe HALT/spin placeholder instead of being
;   left as fall-through NOPs, since real code placed after them would
;   otherwise be reachable (and misinterpreted as an instruction stream)
;   by a stray RST or NMI. This does NOT yet handle flash execute-in-place
;   semantics (.data still needs a load-address/copy step at startup,
;   the way the SAMD21 ports do) — that is future work for real standalone
;   hardware bring-up, not solved by this scaffold.
;
; Assembled with ez80-none-elf-clang (LLVM integrated assembler) via
;   clang --target=ez80-none-elf -x assembler-with-cpp
;
; Provides:  _start  — program entry point (named in AgonLight2*.ld)
; Requires:  main    — C  int main(void)  in AgonLight2/main.c
;            __bss_start, __bss_size — absolute symbols from linker script

    .section .text.boot,"ax"
    .assume ADL=1
    .globl  _start

_start:
    JP  realStart

#ifdef NANO_OS_AGON_LIGHT_2_STANDALONE

    .org 0x08
    JP  unhandledVector
    .org 0x10
    JP  unhandledVector
    .org 0x18
    JP  unhandledVector
    .org 0x20
    JP  unhandledVector
    .org 0x28
    JP  unhandledVector
    .org 0x30
    JP  unhandledVector
    .org 0x38
    JP  unhandledVector
    .org 0x66                   ; NMI
    JP  unhandledVector
    .org 0x70

unhandledVector:
    HALT
    JR  unhandledVector

#else

    .align 6                    ; pad to offset 0x40 for the MOS header below
    .byte 'M'
    .byte 'O'
    .byte 'S'
    .byte 0                     ; version (unused by MOS)
    .byte 1                     ; execution mode: 1 = ADL (24-bit)

#endif

realStart:
    DI                          ; disable maskable interrupts while setting up

    ; Map eZ80F92 on-chip 8 KB SRAM to 0xFFE000.
    ; RAMADL (0xB5) holds address bits [23:16]; for 0xFFE000 that is 0xFF.
    ; Setting RAMCTL (0xB4) bit 7 (RAMEN) arms the mapping.
    LD  A, 0xFF
    OUT0 (0xB5), A
    IN0  A, (0xB4)
    SET  7, A
    OUT0 (0xB4), A

    ; Point the stack at the top of external SRAM.
    ; External SRAM: 0x040000–0x0BFFFF (512 KB).
    LD  SP, 0x0C0000

    ; Zero BSS.  Skip the loop when __bss_size == 0.
    LD  HL, __bss_start
    LD  BC, __bss_size
    LD  A, B
    OR  C
    JR  Z, .bss_done
    LD  E, 0                     ; E holds the zero byte; the loop below must
                                 ; not touch E, since A gets clobbered by the
                                 ; BC-zero check on every pass
.bss_loop:
    LD  (HL), E
    INC HL
    DEC BC
    LD  A, B
    OR  C
    JR  NZ, .bss_loop
.bss_done:

    ; Re-enable maskable interrupts now that setup is complete.
    EI

    ; Jump into the C entry point.
    CALL _main

    ; NanoOs never returns from main().  Halt the CPU if it somehow does.
.hang:
    HALT
    JR  .hang

    .extern __bss_start
    .extern __bss_size
    .extern _main
