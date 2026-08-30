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

;;; @file Interrupts.asm
;;;
;;; @brief eZ80F92 mode 2 interrupt vector table for NanoOs on the Agon Light 2.
;;;
;;; @details
;;; Once NanoOs owns the machine it stops calling MOS and never returns to it,
;;; so it carries its own interrupt vector table baked into the OS image rather
;;; than hooking whatever MOS left behind.
;;;
;;; eZ80F92 mode 2 rules (these are NOT the same as the eZ80F91):
;;;   - The vectoring address is { I[7:0], IVECT[7:0] }.  I is an 8-bit page
;;;     number, so the table must sit on a 256-byte boundary WITHIN THE FIRST
;;;     64 KB of the address space.
;;;   - Each entry is 2 bytes: the low 16 bits of an ISR address.  The CPU
;;;     jumps to { 00h, entry }, so every ISR - or a trampoline that does a
;;;     JP.LIL to the real handler - must ALSO live in the first 64 KB.
;;;   - 128 entries => a full 256-byte page.
;;;
;;; On the Agon Light 2 the first 64 KB is on-chip flash (external RAM starts
;;; at 0x040000), so this only works because NanoOs is the primary firmware:
;;; its own image occupies low flash and ld/AgonLight2.ld places .ivt and
;;; .text.ivt below 0x010000.
;;;
;;; The table lives in its own initialised section (.ivt), kept in the image by
;;; KEEP(*(.ivt)) in ld/AgonLight2.ld.  It ends up in flash, so it is
;;; read-only at run time - a vector is bound only by editing its .short line
;;; here at build time, not by any runtime call.  Every slot starts out
;;; pointing at _defaultIsr; as each driver is written, replace the .short on
;;; that slot's line with the ISR symbol (or a trampoline symbol), and make
;;; sure that target is linked into .text.ivt so it lands below 0x010000.
;;;
;;; If a vector ever needs a handler that changes at run time, give that slot a
;;; fixed trampoline in .text.ivt that does JP.LIL through a RAM pointer, and
;;; swap the pointer - the table entry itself stays put.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1

;;; eZ80F92 maskable interrupt vector offsets - byte offset of the 2-byte slot
;;; from the start of the table.  From the eZ80F92 Product Specification and
;;; Agon MOS src/equs.inc.  Confirm the timer / SPI / I2C values against the
;;; Product Specification before relying on them.
IVECT_PRT0      .equ 0x0A       ; Timer 0 / PRT 0
IVECT_PRT1      .equ 0x0C       ; Timer 1 / PRT 1
IVECT_PRT2      .equ 0x0E       ; Timer 2 / PRT 2
IVECT_PRT3      .equ 0x10       ; Timer 3 / PRT 3
IVECT_PRT4      .equ 0x12       ; Timer 4 / PRT 4
IVECT_PRT5      .equ 0x14       ; Timer 5 / PRT 5
IVECT_UART0     .equ 0x18
IVECT_UART1     .equ 0x1A
IVECT_I2C       .equ 0x1C
IVECT_SPI       .equ 0x1E
IVECT_PB0       .equ 0x30       ; GPIO port B pin 0 .. port D pin 7 at 0x5E
IVECT_PB1       .equ 0x32       ; VDP VBLANK line on the Agon

;;; ------------------------------------------------------------------------
;;; The hardware vector table: 128 slots of 2 bytes each = one 256-byte page.
;;; Placed below 0x010000 and 256-aligned by ld/AgonLight2.ld.
;;; ------------------------------------------------------------------------
.section .ivt,"aw",@progbits
.align 8                        ; 2^8 = 256-byte boundary
.global __nanoOsIvt
__nanoOsIvt:
    ;      offset  eZ80F92 source
    .short _defaultIsr   ; 0x00
    .short _defaultIsr   ; 0x02
    .short _defaultIsr   ; 0x04
    .short _defaultIsr   ; 0x06
    .short _defaultIsr   ; 0x08
    .short clockTimerIsr ; 0x0A  PRT0 / Timer0
    .short _defaultIsr   ; 0x0C  PRT1   <- prt1Tramp once the C dispatcher exists
    .short _defaultIsr   ; 0x0E  PRT2   <- prt2Tramp        "
    .short _defaultIsr   ; 0x10  PRT3   <- prt3Tramp        "
    .short _defaultIsr   ; 0x12  PRT4   <- prt4Tramp        "
    .short _defaultIsr   ; 0x14  PRT5   <- prt5Tramp        "
    .short _defaultIsr   ; 0x16
    .short _defaultIsr   ; 0x18  UART0
    .short _defaultIsr   ; 0x1A  UART1
    .short _defaultIsr   ; 0x1C  I2C
    .short _defaultIsr   ; 0x1E  SPI
    .short _defaultIsr   ; 0x20
    .short _defaultIsr   ; 0x22
    .short _defaultIsr   ; 0x24
    .short _defaultIsr   ; 0x26
    .short _defaultIsr   ; 0x28
    .short _defaultIsr   ; 0x2A
    .short _defaultIsr   ; 0x2C
    .short _defaultIsr   ; 0x2E
    .short _defaultIsr   ; 0x30  GPIO PB0
    .short _defaultIsr   ; 0x32  GPIO PB1 (VDP VBLANK)
    .short _defaultIsr   ; 0x34  GPIO PB2
    .short _defaultIsr   ; 0x36  GPIO PB3
    .short _defaultIsr   ; 0x38  GPIO PB4
    .short _defaultIsr   ; 0x3A  GPIO PB5
    .short _defaultIsr   ; 0x3C  GPIO PB6
    .short _defaultIsr   ; 0x3E  GPIO PB7
    .short _defaultIsr   ; 0x40  GPIO PC0
    .short _defaultIsr   ; 0x42  GPIO PC1
    .short _defaultIsr   ; 0x44  GPIO PC2
    .short _defaultIsr   ; 0x46  GPIO PC3
    .short _defaultIsr   ; 0x48  GPIO PC4
    .short _defaultIsr   ; 0x4A  GPIO PC5
    .short _defaultIsr   ; 0x4C  GPIO PC6
    .short _defaultIsr   ; 0x4E  GPIO PC7
    .short _defaultIsr   ; 0x50  GPIO PD0
    .short _defaultIsr   ; 0x52  GPIO PD1
    .short _defaultIsr   ; 0x54  GPIO PD2
    .short _defaultIsr   ; 0x56  GPIO PD3
    .short _defaultIsr   ; 0x58  GPIO PD4
    .short _defaultIsr   ; 0x5A  GPIO PD5
    .short _defaultIsr   ; 0x5C  GPIO PD6
    .short _defaultIsr   ; 0x5E  GPIO PD7
    ; 0x60..0xFE - no documented source; padded to a full 256-byte page.
    .rept 80
    .short _defaultIsr
    .endr

;;; ------------------------------------------------------------------------
;;; Code that must live in the first 64 KB alongside the table: the default
;;; ISR, and eventually any per-vector trampolines.  The linker script keeps
;;; .text.ivt directly after .ivt.
;;; ------------------------------------------------------------------------
.section .text.ivt,"ax",@progbits
.global _enableInterrupts
.global _defaultIsr

;;; @fn void enableInterrupts(void)
;;;
;;; @brief Bring maskable interrupts online: point the I register at the vector
;;; table's page, select interrupt mode 2, and enable interrupts.
;;;
;;; @details This is a one-time bring-up call, not a critical-section re-enable
;;; primitive - it reloads I and re-selects the mode, which a plain "ei" would
;;; not.  Call it exactly once, as the very last step of HAL initialisation,
;;; after every peripheral has been configured: the moment it returns, any
;;; armed source can fire and must find a real handler in its slot.  Interrupts
;;; are disabled from reset through to this call (boot/AgonLight2/Boot.asm no
;;; longer enables them).
;;;
;;; @return This function returns no value.  Overwrites AF and HL.
_enableInterrupts:
    ld      hl, __nanoOsIvt    ; 256-aligned and below 0x010000, so H holds
    ld      a, h               ; bits 15:8 = the 8-bit vector page number
    ld      i, a
    im      2
    ei                         ; takes effect after the following instruction
    ret

;;; @fn void defaultIsr(void)
;;;
;;; @brief Catch-all handler for any vector a driver has not claimed.
;;;
;;; @details Matches the RST/NMI stub behaviour in boot/AgonLight2/Boot.asm:
;;; stop hard and obviously instead of silently returning.  An unexpected
;;; interrupt means a peripheral was left armed or a slot is wrong, and a
;;; silent reti.l would just livelock on the re-asserting source.
_defaultIsr:
    di
    halt
    jr      _defaultIsr

;;; ========================================================================
;;; PRT1..PRT5 one-shot timer trampolines (FIRST-PASS DRAFT - not yet wired)
;;; ========================================================================
;;;
;;; These are the eZ80 analogue of TC3_Handler / TC4_Handler +
;;; arduinoSamD21x18ATimerInterruptHandler{0,1} in
;;; src/hal/HalArduinoSamD21x18A.cpp.  Two things force a trampoline here:
;;;
;;;   1. Reach.  An IM2 vector slot is 2 bytes and the CPU jumps to
;;;      { 00h, entry }, so the target must live below 0x010000.  The real
;;;      timer handlers are C, linked into the main .text well above 64 KB;
;;;      this stub sits in .text.ivt (kept low by ld/AgonLight2.ld) and is
;;;      what the slot actually points at.
;;;
;;;   2. Context.  The HAL timer callback for these devices is the scheduler's
;;;      preemption callback (forceYield -> processYieldTo), which performs a
;;;      coroutine context switch and does NOT return to the interrupted point
;;;      until that process is next scheduled.  It has to run with interrupts
;;;      ENABLED, or PRT0 (the millisecond clock) and every other timer stall
;;;      for the whole duration of the preemption.  On the SAMD21 that means
;;;      returning from the Cortex-M0 exception first (RETURN_TO_HANDLER) and
;;;      running the handler in thread mode.  The eZ80 has no handler mode - an
;;;      IM2 interrupt only clears IEF1 and pushes a 3-byte return PC - so the
;;;      trampoline just clears the timer's IRQ, does `ei`, and calls the
;;;      handler.  If the callback switches away, this trampoline's frame
;;;      (~28 bytes: the register saves + the interrupt-pushed PC) simply
;;;      freezes on the preempted process's stack; when the coroutine
;;;      machinery later restores that process's SP/PC, execution returns from
;;;      the `call` below, the frame unwinds, and `reti` pops the pushed PC to
;;;      resume exactly where the timer fired.  No _savedContext-style global
;;;      is needed: every trampoline invocation is self-contained on the stack,
;;;      so nesting / re-entrancy is naturally safe.
;;;
;;; TODO before wiring the .short slots to prtNTramp:
;;;   - Add agonLight2TimerInterruptHandler1..5() to HalAgonLight2.c, each a
;;;     thin wrapper around a shared agonLight2TimerInterruptHandler(deviceId)
;;;     that clears the hardwareTimers[deviceId] bookkeeping (active/deadline)
;;;     and invokes its callback - mirror arduinoSamD21x18ATimerInterruptHandler.
;;;   - Add a hardwareTimers[]-equivalent plus PRT-backed initDevice /
;;;     configOneShot / cancel / cancelAndGet (PRT_MODE=0 single-pass, so the
;;;     PRT auto-disables after one shot; the `in0` below still has to clear
;;;     the pending PRT_IRQ so `ei` does not immediately re-enter).
;;;   - Then change the five ".short _defaultIsr ; 0x0C..0x14" lines above to
;;;     ".short prtNTramp".
;;;
;;; Tradeoff to revisit: `ei` before the call keeps this frame parked on the
;;; interrupted process's 1 KB stack across the entire preemption.  Bounded by
;;; the number of processes; acceptable for a first pass.

;;; Weak placeholders so this file links before HalAgonLight2.c provides the
;;; real handlers.  Any strong C definition of agonLight2TimerInterruptHandlerN
;;; overrides its stub.  They are dead until a .short slot above is repointed;
;;; delete this block once the C side lands.
.section .text,"ax",@progbits
.weak _agonLight2TimerInterruptHandler1
.weak _agonLight2TimerInterruptHandler2
.weak _agonLight2TimerInterruptHandler3
.weak _agonLight2TimerInterruptHandler4
.weak _agonLight2TimerInterruptHandler5
_agonLight2TimerInterruptHandler1:
_agonLight2TimerInterruptHandler2:
_agonLight2TimerInterruptHandler3:
_agonLight2TimerInterruptHandler4:
_agonLight2TimerInterruptHandler5:
    ret

.section .text.ivt,"ax",@progbits
.global prt1Tramp
.global prt2Tramp
.global prt3Tramp
.global prt4Tramp
.global prt5Tramp

;;; eZ80F92 PRT control-register I/O addresses.  A read of TMRn_CTL clears that
;;; timer's PRT_IRQ (bit 7).  TMRn_CTL = 0x80 + 3*n.
TMR1_CTL    .equ 0x083
TMR2_CTL    .equ 0x086
TMR3_CTL    .equ 0x089
TMR4_CTL    .equ 0x08C
TMR5_CTL    .equ 0x08F

;;; PRT_TRAMPOLINE name, ctlPort, handler
;;;   name    - the label the IVT slot points at
;;;   ctlPort - that timer's TMRn_CTL, read to clear the pending PRT_IRQ
;;;   handler - the C entry point (deviceId is baked into the entry point, as
;;;             in the SAMD21 ...Handler0 / ...Handler1 split, so nothing has to
;;;             be marshalled across the eZ80 C ABI here)
.macro PRT_TRAMPOLINE name, ctlPort, handler
\name:
    push    af
    push    bc
    push    de
    push    hl
    push    ix                 ; over-saved for the first pass; IX/IY are
    push    iy                 ; probably callee-saved by the C ABI + preserved
                               ; across processYieldTo, so these two may go
    in0     a, (\ctlPort)      ; read TMRn_CTL -> clears this timer's PRT_IRQ
    ei                         ; handler (and its context switch) runs with
                               ; interrupts live - see the note above
    call    \handler
    di                         ; atomic teardown
    pop     iy
    pop     ix
    pop     hl
    pop     de
    pop     bc
    pop     af
    ei                         ; effective after the reti below
    reti                       ; plain RETI (ED 4D): MADL=0 / pure ADL, so the
                               ; interrupt pushed only a 3-byte PC
.endm

    PRT_TRAMPOLINE prt1Tramp, TMR1_CTL, _agonLight2TimerInterruptHandler1
    PRT_TRAMPOLINE prt2Tramp, TMR2_CTL, _agonLight2TimerInterruptHandler2
    PRT_TRAMPOLINE prt3Tramp, TMR3_CTL, _agonLight2TimerInterruptHandler3
    PRT_TRAMPOLINE prt4Tramp, TMR4_CTL, _agonLight2TimerInterruptHandler4
    PRT_TRAMPOLINE prt5Tramp, TMR5_CTL, _agonLight2TimerInterruptHandler5
