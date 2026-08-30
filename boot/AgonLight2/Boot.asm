;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 
;                        Copyright (c) 2026 Brian Card
; 
;  Permission is hereby granted, free of charge, to any person obtaining a
;  copy of this software and associated documentation files (the "Software"),
;  to deal in the Software without restriction, including without limitation
;  the rights to use, copy, modify, merge, publish, distribute, sublicense,
;  and;or sell copies of the Software, and to permit persons to whom the
;  Software is furnished to do so, subject to the following conditions:
; 
;  The above copyright notice and this permission notice shall be included
;  in all copies or substantial portions of the Software.
; 
;  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
;  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
;  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
;  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
;  DEALINGS IN THE SOFTWARE.
; 
;                                  Brian Card
;                        https://github.com/brian-card
; 
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;; @file Boot.asm
;;;
;;; @brief NanoOs startup for Agon Light 2.
;;;
;;; @details
;;; NanoOs is the primary firmware: entry is the eZ80 reset vector at address
;;; 0x000000, and it replaces MOS entirely (no MOS load header). The eZ80
;;; leaves RESET in Z80 (16-bit) mode, so _start's first instruction is a
;;; JP.LIL that both jumps to realStart and switches the CPU into ADL (24-bit)
;;; mode; everything from realStart on runs in ADL mode.
;;;
;;; The low-memory RST (0x08/0x10/0x18/0x20/0x28/0x30/0x38) and NMI (0x66)
;;; vector slots are stubbed with a safe HALT/spin placeholder instead of
;;; being left as fall-through NOPs, since real code placed after them would
;;; otherwise be reachable (and misinterpreted as an instruction stream) by a
;;; stray RST or NMI.
;;;
;;; Flash execute-in-place: .text and .rodata run / are read straight from
;;; on-chip flash.  .data has its load image in flash (right after .text) and
;;; its run location in external SRAM; realStart copies it there and then zeroes
;;; .bss, both in external SRAM (see ld/AgonLight2.ld for the memory model).
;;;
;;; Assembled with ez80-none-elf-clang (LLVM integrated assembler) via
;;;   clang --target=ez80-none-elf -x assembler-with-cpp
;;;
;;; Provides:  _start  — program entry point (named in ld/AgonLight2.ld)
;;; Requires:  main    — C  int main(void)  in AgonLight2/main.c
;;;            __bss_start, __bss_size — absolute symbols from linker script
;;;
;;; @note This file was generated with assistance from claude.ai.

.section .text.boot,"ax"
.globl  _start

; The eZ80 executes this first instruction in Z80 mode (ADL=0) straight out of
; RESET.  JP.LIL reads a 24-bit target, jumps, and sets ADL=1.  Assembling it
; under `.assume ADL=0` is what makes the assembler emit the 0x5B mode-switch
; prefix; under `.assume ADL=1` it would encode a plain ADL-mode JP that does
; not switch modes and would be mis-decoded at reset.
.assume ADL=0
_start:
    JP.LIL  realStart

.assume ADL=1

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

realStart:
    ; --- external-RAM integrity canary -------------------------------------
    ; Stamp the known pattern 0x4ABC4ABC4ABC4ABC at __data_bss_limit (0x049000,
    ; the 36 KB mark of external SRAM — the byte just past the 4 KB .bss/.data
    ; reservation) as the very first thing we do, before .data is copied or
    ; .bss is cleared.  External SRAM needs no controller setup, so this is
    ; safe this early.  halAgonLight2Init() re-reads these 8 bytes as its final
    ; step; a mismatch means .bss/.data overflowed and corrupted memory.
    LD  HL, __data_bss_limit
    LD  B, 4
.canary_loop:
    LD  (HL), 0xBC
    INC HL
    LD  (HL), 0x4A
    INC HL
    DEC B
    JR  NZ, .canary_loop

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

    ; Copy initialised data from its load image in flash (__data_load__, laid
    ; down by the linker right after .text) to its run location in external
    ; SRAM (__data_start__).  Skip the loop when __data_size == 0.
    LD  HL, __data_load__
    LD  DE, __data_start__
    LD  BC, __data_size
    LD  A, B
    OR  C
    JR  Z, .data_done
.data_loop:
    LD  A, (HL)
    LD  (DE), A
    INC HL
    INC DE
    DEC BC
    LD  A, B
    OR  C
    JR  NZ, .data_loop
.data_done:

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

    ; Interrupts are left DISABLED here.  NanoOs installs its own mode 2
    ; interrupt vector table (src/hal/AgonLight2/Interrupts.asm) during HAL
    ; bring-up and re-enables interrupts itself once the table and the
    ; per-peripheral handlers are in place.  Enabling them here would run on
    ; whatever vector table MOS left behind.

    ; Jump into the C entry point.
    CALL _main

    ; NanoOs never returns from main().  Halt the CPU if it somehow does.
.hang:
    HALT
    JR  .hang

    .extern __bss_start
    .extern __bss_size
    .extern __data_start__
    .extern __data_load__
    .extern __data_size
    .extern __data_bss_limit
    .extern _main
