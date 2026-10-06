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
;;; UART0 on the Agon Light 2, which uses the eZ80F92 CPU.  RX is interrupt-
;;; driven into a ring buffer, as on UART1.  TX stays polled because it has to
;;; wait on CTS from the VDP, which an interrupt handler can't do.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1

.global _agonLight2ConfigureUart0Impl
.extern _halCommonUartMarkReady
.global _agonLight2PollUart0Impl
.global _agonLight2ReadUart0Impl
.global _agonLight2WriteUart0Impl
.global uart0Isr

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

UART0_IER_RX_BIT .equ 0x01 ; ERBI - enable "receive data available" interrupt

;; -- RX ring buffer: uart0Isr is the only producer, the Poll/Read impls the
;; only consumers.
UART0_RING_SIZE .equ 64            ; must be a power of two
UART0_RING_MASK .equ (UART0_RING_SIZE - 1)

.bss
uart0RxHead: .space 1              ; written only by uart0Isr
uart0RxTail: .space 1              ; written only by the Poll/Read impls
uart0RxBuf:  .space UART0_RING_SIZE

.text

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

    ;; Interrupts for this UART are off, so the ring can be reset safely.
    ld      (uart0RxHead), a
    ld      (uart0RxTail), a

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

    ld      a, UART0_IER_RX_BIT
    out0    (UART0_IER), a

    pop     ix
    ret

;; -- int agonLight2PollUart0Impl(void) ----------------------------
;;    Pops one byte from the RX ring.  Returns it in HL, or -1 if the ring is
;;    empty.  di/ei for the same reason as agonLight2PollUart1Impl.
_agonLight2PollUart0Impl:
    di
    ld      a, (uart0RxTail)
    ld      hl, uart0RxHead
    cp      (hl)
    jr      z, .noChar          ; tail == head -> ring empty

    ld      bc, 0               ; clears BCU too - see agonLight2PollUart1Impl
    ld      c, a
    ld      hl, uart0RxBuf
    add     hl, bc
    ld      b, (hl)             ; b = the byte

    inc     a
    and     UART0_RING_MASK
    ld      (uart0RxTail), a
    ei

    ld      hl, 0
    ld      l, b
    ret

.noChar:
    ei
    ld      hl, -1
    ret

;; -- size_t agonLight2ReadUart0Impl(uint8_t *data, size_t length) ---------
;;    data at sp+3, length at sp+6.  Copies up to length bytes from the RX
;;    ring into data, advances uart0RxTail past them, and returns the number
;;    copied in HL.  Same structure as agonLight2ReadUart1Impl.
_agonLight2ReadUart0Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

    di
    ld      a, (uart0RxTail)
    ld      c, a                ; c = tail
    ld      a, (uart0RxHead)
    sub     c
    and     UART0_RING_MASK     ; a = bytes available
    ld      de, 0               ; clears DEU too - see agonLight2PollUart1Impl
    ld      e, a
    ld      hl, (ix+9)          ; hl = length
    or      a
    sbc     hl, de
    jr      nc, .readCountReady ; length >= available -> take all of it
    ld      a, (ix+9)           ; length < available, so it fits in a byte
.readCountReady:
    or      a
    jr      z, .readEmpty

    ld      b, a                ; b = bytes to copy
    ld      hl, uart0RxBuf
    ld      e, c                ; DEU/D still clear from above
    add     hl, de              ; hl = &uart0RxBuf[tail]
    ld      de, (ix+6)          ; de = data
.readLoop:
    ld      a, (hl)
    ld      (de), a
    inc     de
    inc     hl
    ld      a, c
    inc     a
    and     UART0_RING_MASK
    ld      c, a
    jr      nz, .readNoWrap
    ld      hl, uart0RxBuf
.readNoWrap:
    djnz    .readLoop

    ld      a, c
    ld      (uart0RxTail), a
    ei

    ex      de, hl              ; hl = one past the last byte written
    ld      de, (ix+6)
    or      a
    sbc     hl, de              ; hl = bytes copied
    pop     ix
    ret

.readEmpty:
    ei
    ld      hl, 0
    pop     ix
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

;; -- uart0Isr --------------------------------------------------------------
;;    Called (interrupts off) from uart0Tramp in Interrupts.asm.  Drains the
;;    RX FIFO into uart0RxBuf and, if it received anything, calls the device's
;;    registered callback.
uart0Isr:
    in0     a, (UART0_LSR)
    and     0x01                ; DR - receiver data ready
    ret     z

.uart0IsrRxLoop:
    in0     a, (UART0_RBR)      ; also clears DR for this byte
    call    uart0RxPushByte
    in0     a, (UART0_LSR)
    and     0x01
    jr      nz, .uart0IsrRxLoop

    ;; halCommonUartMarkReady(0): an int32_t takes two 3-byte stack slots,
    ;; high byte pushed first (de), low 24 bits pushed last (hl).
    ld      de, 0
    ld      hl, 0
    push    de
    push    hl
    call    _halCommonUartMarkReady
    pop     hl
    pop     de
    ret

;; -- uart0RxPushByte ---------------------------------------------------
;;    a = received byte on entry.  Pushes onto uart0RxBuf, dropping the byte
;;    if the ring is full (never overwrites unread data).  Clobbers af/bc/de/hl.
uart0RxPushByte:
    ld      b, a                ; b = byte to store
    ld      a, (uart0RxHead)
    inc     a
    and     UART0_RING_MASK
    ld      hl, uart0RxTail
    cp      (hl)
    ret     z                   ; would collide with tail -> full, drop byte

    ld      a, (uart0RxHead)    ; a = slot to write (the OLD head)
    ld      de, 0               ; clears DEU too - see agonLight2PollUart1Impl
    ld      e, a
    ld      hl, uart0RxBuf
    add     hl, de
    ld      (hl), b             ; store the byte

    ld      a, (uart0RxHead)
    inc     a
    and     UART0_RING_MASK
    ld      (uart0RxHead), a
    ret
