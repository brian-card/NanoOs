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

;;; @file Clock.asm
;;;
;;; @brief eZ80 assembly for implementing a sub-millisecond resolution clock on
;;; the eZ80.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1

;;; @var __nanosecondCount
;;;
;;; @brief 64-bit, little-endian count of the number of nanoseconds since boot.
;;; Incremented by 62500 (62.5 microseconds) every time _clockTimerIsr fires.
;;;
;;; Lives in .bss (zeroed by Boot.asm's BSS-clear loop), not .text - .text is
;;; ROM in a future standalone/flash build and this counter is written at
;;; runtime.
.bss
__nanosecondCount:     .space 8        ; nanoseconds since boot, 64-bit LE

.text
.global _initClockTimer
.global _clockTimerIsr
.global _readClock

;; -- eZ80F92 Timer0 registers
;; On the eZ80F92 the counter and the reload value share an address: a read
;; returns the live counter, a write sets the reload register.  We're not
;; ever reading the counter value in this code, so no aliases are provided for
;; that.
TMR0_CTL    .equ 0x080     ; R/W  control register
TMR0_RR_L   .equ 0x081     ; W    reload value   - low byte
TMR0_RR_H   .equ 0x082     ; W    reload value   - high byte

;;; @fn void initClockTimer(void)
;;;
;;; @brief Initializes timer 0 to fire every 62.5 microseconds.
;;;
;;; @return This function returns no value.
_initClockTimer:
    ld      a, 0x00         ; Clear timer 0's configuration while we're working
    out0    (TMR0_CTL), a

    ld      a, 0x48         ; reload low byte  = 0x48  (72 decimal)
    out0    (TMR0_RR_L), a
    ld      a, 0x00         ; reload high byte = 0x00
    out0    (TMR0_RR_H), a

    ld      a, 0x57         ; IRQ_EN=1, PRT_MODE=1 (continuous)
                            ; CLK_DIV=01 (/16), RST_EN=1, EN=1
    out0    (TMR0_CTL), a

    ret

;;; @fn void clockTimerIsr(void)
;;;
;;; @brief Interrupt service routine (ISR) that runs every time timer 0 fires.
;;; Per the configuration in initClockTimer, This fires exactly every 62.5
;;; microseconds.
;;;
;;; @return This function returns no value.
_clockTimerIsr:
    push    af
    push    hl
    push    bc              ; the wide adds below use bc (an ISR must save it)
    ld      bc, 0           ; zero addend for the carry-propagation adds below

    in0     a, (TMR0_CTL)   ; read of TMR0_CTL clears the PRT_IRQ flag (bit 7)

    ; --- add 62500 (0xf424) to the low 16 bits ---
    ld      hl, __nanosecondCount
    ld      a, (hl)
    add     a, 0x24         ; low byte of 62500
    ld      (hl), a
    inc     hl
    ld      a, (hl)
    adc     a, 0x0f4        ; high byte of 62500, plus carry from above
    ld      (hl), a
    jr      nc, tickDone    ; no carry out — common case, done

    ; --- propagate the carry through the upper 6 bytes ---
    ; ADL mode makes hl and bc 24-bit, so bytes 2..4 and 5..7 each ripple in a
    ; single "adc hl, bc" (bc is 0, so only the incoming carry is added).  ld
    ; does not touch the carry flag, so it survives between the two chunks.
    ld      hl, (__nanosecondCount + 2)
    adc     hl, bc
    ld      (__nanosecondCount + 2), hl
    jr      nc, tickDone
    ld      hl, (__nanosecondCount + 5)
    adc     hl, bc
    ld      (__nanosecondCount + 5), hl

tickDone:
    pop     bc
    pop     hl
    pop     af
    ei                      ; re-enable interrupts on the instruction after this
    reti.l

;;; @fn void readClock(int64_t *returnValue)
;;;
;;; @brief Atomically read the number of nanoseconds since boot.
;;;
;;; @details Interrupts are disabled only for the 8-byte copy, so the timer ISR
;;; cannot update __nanosecondCount mid-read and hand back a torn value that
;;; straddles a carry.  The caller's interrupt-enable state is preserved: if
;;; interrupts were already disabled on entry they are left disabled on exit.
;;; This makes the routine safe to call from an ISR or a critical section as
;;; well as from ordinary thread context.
;;;
;;; @param returnValue The address of the 64-bit memory location to store the
;;;   clock value in.  Located at sp+3.
;;;
;;; @return This function returns no value.  Overwrites the AF, BC, DE, and HL
;;; registers.
_readClock:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      de, (ix+6)      ; Get the address of the return value
    ld      hl, __nanosecondCount
    ld      bc, 8           ; Copy 8 bytes (64 bits)

    ld      a, i            ; side effect: P/V := IEF2 (1 = interrupts were on)
                            ; A takes the I register (IVT base) and is discarded
    push    af              ; preserve P/V — LDIR forces it to 0 when BC hits 0
    di                      ; block the timer ISR so the 8-byte read can't tear
    ldir
    pop     af              ; recover the saved P/V
    jp      po, tickRead    ; P/V = 0: caller had interrupts off, leave them off
    ei

tickRead:
    pop     ix
    ret

