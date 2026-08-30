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
.global _agonLight2SetSpiBrgImpl
.global _agonLight2SpiTransfer8Impl

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

;; -- void agonLight2ConfigureSpiImpl(uint16_t divisor) -------------
;;    divisor passed at sp+3 (low byte), sp+4 (high byte)
_agonLight2ConfigureSpiImpl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Set PB7 (MOSI), PB6 (MISO), and PB3 (CLK) to Alternate Function mode
    ;; (DDR = 1, ALT1 = 0, ALT2 = 1).  "Alternate Function mode" in this case
    ;; means "SPI peripheral mode".
    in0     a, (PB_DDR)
    or      a, 0xC8
    out0    (PB_DDR), a

    in0     a, (PB_ALT1)
    and     a, 0x37
    out0    (PB_ALT1), a

    in0     a, (PB_ALT2)
    or      a, 0xC8
    out0    (PB_ALT2), a

    ;; PB2 is the SPI peripheral's hardware SS input.  It must be configured
    ;; as a GPIO output and held permanently high for the on-chip SPI hardware
    ;; to work properly in master mode.  The SD card's chip-select line is a
    ;; separate line that's configured directly in the HAL C code.
    in0     a, (PB_DR)
    or      a, 0x04
    out0    (PB_DR), a

    in0     a, (PB_ALT1)
    and     a, 0xFB
    out0    (PB_ALT1), a

    in0     a, (PB_ALT2)
    and     a, 0xFB
    out0    (PB_ALT2), a

    in0     a, (PB_DDR)
    and     a, 0xFB
    out0    (PB_DDR), a

    ;; Set the speed of the SPI bus.  The caller is responsible for passing the
    ;; desired divisor into this function, where:
    ;;
    ;; divisor = system clock / (2 * SPI clock)
    ld      a, (ix+6)
    out0    (SPI_BRG_L), a
    ld      a, (ix+7)
    out0    (SPI_BRG_H), a

    ;; Set SPIEN | MASTEREN, CPOL=0 CPHA=0
    ld      a, 0x30
    out0    (SPI_CTL), a

    pop     ix
    ret

;; -- void agonLight2SetSpiBrgImpl(uint16_t divisor) ---------------
;;    divisor passed at sp+3 (low byte), sp+4 (high byte).
;;
;;    Reprograms ONLY the SPI baud-rate generator.  The pin mux and SPI_CTL
;;    (mode / CPOL / CPHA / enable) set up by _agonLight2ConfigureSpiImpl are
;;    left untouched - re-muxing PB3/PB6/PB7 or rewriting SPI_CTL on an idle
;;    bus glitches the clock line, which corrupts the first byte of the next
;;    transfer.  Devices sharing the bus at different speeds call this on every
;;    transfer start; the full configure runs once at device-configure time.
_agonLight2SetSpiBrgImpl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      a, (ix+6)
    out0    (SPI_BRG_L), a
    ld      a, (ix+7)
    out0    (SPI_BRG_H), a

    pop     ix
    ret

;; -- int agonLight2SpiTransfer8Impl(uint8_t c) ---------------------
;;    c passed at sp+3
_agonLight2SpiTransfer8Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      a, (ix+6)
    out0    (SPI_TSR), a

.spiTransferWait:
    in0     a, (SPI_SR)
    bit     7, a
    jr      z, .spiTransferWait

    in0     a, (SPI_RBR)
    ld      hl, 0
    ld      l, a

    pop     ix
    ret
