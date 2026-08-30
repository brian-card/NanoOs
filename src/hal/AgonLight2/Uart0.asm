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

;;; @file Uart0.asm
;;;
;;; @brief eZ80 assembly implementation of functionality for communicating with
;;; UART0 on the Agon Light 2, which uses the eZ80F92 CPU.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1
.text

.global _agonLight2ConfigureUart0Impl
.global _agonLight2PollUart0Impl
.global _agonLight2WriteUart0Impl

;; -- eZ80F92 Port D GPIO registers ------------------
PD_DR       .equ 0xA2      ; Port D data register
PD_DDR      .equ 0xA3      ; Port D data direction
PD_ALT1     .equ 0xA4      ; Port D alternate function 1
PD_ALT2     .equ 0xA5      ; Port D alternate function 2

;; -- eZ80F92 UART0 registers (base 0xC0) ------------
UART0_THR   .equ 0xC0      ; TX holding register
UART0_RBR   .equ 0xC0      ; RX buffer register
UART0_BRG_L .equ 0xC0      ; BRG divisor low  (DLAB=1)
UART0_IER   .equ 0xC1      ; Interrupt enable
UART0_BRG_H .equ 0xC1      ; BRG divisor high (DLAB=1)
UART0_FCTL  .equ 0xC2      ; FIFO control (write)
UART0_LCTL  .equ 0xC3      ; Line control
UART0_MCTL  .equ 0xC4      ; Modem control
UART0_LSR   .equ 0xC5      ; Line status
UART0_MSR   .equ 0xC6      ; Modem status (bit 4 = CTS)

;; -- void agonLight2ConfigureUart0Impl(uint16_t divisor) ---------------
;;    divisor passed at sp+3 (low byte), sp+4 (high byte)
_agonLight2ConfigureUart0Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Configure PD0 (TxD0), PD1 (RxD0), PD2 (RTS0) and PD3 (CTS0) for UART
    ;; alternate function.  RTS0/CTS0 are wired to the peer (the VDP co-
    ;; processor) for flow control; MOS mux es these alongside TxD/RxD.  The
    ;; eZ80F92 UART has no automatic flow control, so RTS is driven from MCTL
    ;; and CTS is polled from MSR (see the write path below).
    ;; UART mode = ALT1:0, ALT2:1
    in0     a, (PD_ALT1)
    and     0xF0                ; clear bits 0..3
    out0    (PD_ALT1), a

    in0     a, (PD_ALT2)
    or      0x0F                ; set bits 0..3
    out0    (PD_ALT2), a

    ;; Set DDR bits 0..3 to input — peripheral overrides direction
    in0     a, (PD_DDR)
    or      0x0F
    out0    (PD_DDR), a

    ;; Disable UART0 interrupts
    xor     a
    out0    (UART0_IER), a

    ;; Set DLAB to access baud rate divisor registers
    ld      a, 0x80
    out0    (UART0_LCTL), a

    ;; Load divisor from parameter
    ld      a, (ix+6)
    out0    (UART0_BRG_L), a
    ld      a, (ix+7)
    out0    (UART0_BRG_H), a

    ;; 8N1: 8 data bits, no parity, 1 stop bit — clears DLAB
    ld      a, 0x03
    out0    (UART0_LCTL), a

    ;; Enable and reset both FIFOs
    ld      a, 0x07
    out0    (UART0_FCTL), a

    ;; Assert RTS0 (MCTL bit 1) so the peer is permitted to transmit to us.
    ld      a, 0x02
    out0    (UART0_MCTL), a

    pop     ix
    ret

;; -- int agonLight2PollUart0Impl(void) ----------------------------
;;    returns received byte in HL, or -1 if no data ready (non-blocking)
_agonLight2PollUart0Impl:
    in0     a, (UART0_LSR)
    and     0x01                ; DR — data ready
    jr      z, .noChar

    in0     a, (UART0_RBR)
    ld      hl, 0
    ld      l, a
    ret

.noChar:
    ld      hl, -1
    ret

;; -- void agonLight2WriteUart0Impl(uint8_t c) ----------------------
;;    c passed at sp+3
_agonLight2WriteUart0Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Bounded wait for CTS0 (MSR bit 4 set = peer is clear to send).  The
    ;; eZ80F92 UART does not gate TX on CTS itself, so honour it here.  Give up
    ;; after ~8192 polls (~17 ms worst case) and send anyway, so an absent or
    ;; silent peer can slow TX but never wedge it.  BC is caller-saved under the
    ;; eZ80 C ABI.
    ld      bc, 0x2000
.ctsWait:
    in0     a, (UART0_MSR)
    bit     4, a
    jr      nz, .ctsReady
    dec     bc
    ld      a, b
    or      c
    jr      nz, .ctsWait
.ctsReady:

.txWait:
    in0     a, (UART0_LSR)
    and     0x20                ; THRE — transmit holding register empty
    jr      z, .txWait

    ld      a, (ix+6)
    out0    (UART0_THR), a

    pop     ix
    ret
