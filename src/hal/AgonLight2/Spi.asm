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

;;; @file Spi.asm
;;;
;;; @brief eZ80 assembly implementation of functionality for communicating with
;;; the SPI bus on the Agon Light 2, which uses the eZ80F92 CPU.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1
.text

.global _agonLight2ConfigureSpiImpl
.global _agonLight2PollSpiImpl
.global _agonLight2WriteSpiImpl

;; -- eZ80F92 Port B GPIO registers ------------------
PB_DR       .equ 0x9A      ; Port B data register
PB_DDR      .equ 0x9B      ; Port B data direction
PB_ALT1     .equ 0x9C      ; Port B alternate function 1
PB_ALT2     .equ 0x9D      ; Port B alternate function 2

;; -- eZ80F92 SPI registers (base 0xB8) ------------
SPI_BRG_L,  .equ 0xB8
SPI_BRG_H,  .equ 0xB9
SPI_CTL,    .equ 0xBA
SPI_SR,     .equ 0xBB
SPI_TSR,    .equ 0xBC
SPI_RBR,    .equ 0xBC

;; -- void agonLight2ConfigureSpiImpl(uint16_t divisor) ---------------
;;    divisor passed at sp+3 (low byte), sp+4 (high byte)
_agonLight2ConfigureSpiImpl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Set PB7, PB6, PB3, and PB2 to mode 7 (alternate function)
    in0   a, (PB_ALT1)
    or    a, 0xCC
    out0  (PB_ALT1), a

    in0   a, (PB_ALT2)
    or    a, 0xCC
    out0  (PB_ALT2), a

    ;; Set the speed of the SPI bus.  The caller is responsible for passing the
    ;; desired divisor into this function, where:
    ;;
    ;; divisor = system clock / (2 * SPI clock)
    ld    a, (ix+6)
    out0  (SPI_BRG_L), a
    ld    a, (ix+7)
    out0  (SPI_BRG_H), a

    ;; Set SPIEN | MASTEREN, CPOL=0 CPHA=0
    ld    a, 0x30
    out0  (SPI_CTL), a

    pop     ix
    ret


;; -- int agonLight2PollSpiImpl(void) ----------------------------
;;    returns received byte in HL, or -1 if no data ready (non-blocking)
_agonLight2PollSpiImpl:
    in0     a, (UART0_LSR)
    and     0x01                ; DR — data ready
    jr      z, .Lno_char

    in0     a, (UART0_RBR)
    ld      hl, 0
    ld      l, a
    ret

.Lno_char:
    ld      hl, -1
    ret
;; -- void agonLight2WriteSpiImpl(uint8_t c) ----------------------
;;    c passed at ix+6
_agonLight2WriteSpiImpl:
    push    ix
    ld      ix, 0
    add     ix, sp

.Ltx_wait:
    in0     a, (UART0_LSR)
    and     0x20                ; THRE — transmit holding register empty
    jr      z, .Ltx_wait

    ld      a, (ix+6)
    out0    (UART0_THR), a

    pop     ix
    ret
