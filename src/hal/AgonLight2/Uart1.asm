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

;;; @file Uart1.asm
;;;
;;; @brief eZ80 assembly implementation of functionality for communicating with
;;; UART1 on the Agon Light 2, which uses the eZ80F92 CPU.
;;;
;;; @details RX and TX are both interrupt-driven: uart1Isr (called from
;;; uart1Tramp in src/hal/AgonLight2/Interrupts.asm) drains received bytes into
;;; uart1RxBuf and refills the transmit holding register from uart1TxBuf, so
;;; neither direction depends on the caller polling the hardware at the right
;;; moment.  Each is a small single-producer/single-consumer ring buffer:
;;; uart1Isr is the sole producer for RX and sole consumer for TX; everything
;;; below is the reverse.  That split means the plain byte reads/writes to
;;; head/tail need no locking EXCEPT the one place both sides touch UART1_IER
;;; (the impl functions set its TX-empty bit, uart1Isr clears it), which is
;;; guarded with di/ei since it is a read-modify-write.
;;;
;;; Buffer size: 64 bytes per direction (128 bytes total) was chosen to fit
;;; comfortably within the .bss/.data budget in ld/AgonLight2.ld - see
;;; DATA_BSS_REGION_SIZE in src/hal/HalAgonLight2.c, which had only ~300 bytes
;;; of headroom before it was bumped to make room for this.  Revisit if a
;;; buffer this size proves to drop bytes under real load.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1

.global _agonLight2ConfigureUart1Impl
.global _agonLight2PollUart1Impl
.global _agonLight2WriteUart1Impl
.global uart1Isr

;; -- eZ80F92 Port C GPIO registers ------------------
PC_DR       .equ 0x9E      ; Port C data register
PC_DDR      .equ 0x9F      ; Port C data direction (1=input, 0=output)
PC_ALT1     .equ 0xA0      ; Port C alternate function 1
PC_ALT2     .equ 0xA1      ; Port C alternate function 2

;; -- eZ80F92 UART1 registers (base 0xD0) ------------
UART1_THR   .equ 0xD0      ; TX holding register
UART1_RBR   .equ 0xD0      ; RX buffer register
UART1_BRG_L .equ 0xD0      ; BRG divisor low  (DLAB=1)
UART1_IER   .equ 0xD1      ; Interrupt enable
UART1_BRG_H .equ 0xD1      ; BRG divisor high (DLAB=1)
UART1_FCTL  .equ 0xD2      ; FIFO control (write)
UART1_LCTL  .equ 0xD3      ; Line control
UART1_MCTL  .equ 0xD4      ; Modem control
UART1_LSR   .equ 0xD5      ; Line status

UART1_IER_RX_BIT .equ 0x01 ; ERBI - enable "receive data available" interrupt
UART1_IER_TX_BIT .equ 0x02 ; ETBEI - enable "transmit holding reg empty" int

;; -- RX/TX ring buffers: single-producer/single-consumer, see file header --
UART1_RING_SIZE .equ 64            ; must be a power of two
UART1_RING_MASK .equ (UART1_RING_SIZE - 1)

.bss
uart1RxHead: .space 1              ; written only by uart1Isr
uart1RxTail: .space 1              ; written only by _agonLight2PollUart1Impl
uart1RxBuf:  .space UART1_RING_SIZE

uart1TxHead: .space 1              ; written only by _agonLight2WriteUart1Impl
uart1TxTail: .space 1              ; written only by uart1Isr
uart1TxBuf:  .space UART1_RING_SIZE

.text

;; -- void agonLight2ConfigureUart1Impl(uint16_t divisor) ---------------
;;    divisor passed at sp+3 (low byte), sp+4 (high byte)
_agonLight2ConfigureUart1Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Configure PC0 (TxD1) and PC1 (RxD1) for UART alternate function
    ;; UART mode = ALT1:0, ALT2:1
    in0     a, (PC_ALT1)
    and     0xFC                ; clear bits 0,1
    out0    (PC_ALT1), a

    in0     a, (PC_ALT2)
    or      0x03                ; set bits 0,1
    out0    (PC_ALT2), a

    ;; Set DDR bits 0,1 to input — peripheral overrides direction
    in0     a, (PC_DDR)
    or      0x03
    out0    (PC_DDR), a

    ;; Disable UART1 interrupts while reconfiguring
    xor     a
    out0    (UART1_IER), a

    ;; Reset the ring buffers - safe to do unconditionally since interrupts
    ;; for this UART are off until enableInterrupts() runs at the end of HAL
    ;; init, and this covers being re-configured after the fact too.
    ld      a, 0
    ld      (uart1RxHead), a
    ld      (uart1RxTail), a
    ld      (uart1TxHead), a
    ld      (uart1TxTail), a

    ;; Set DLAB to access baud rate divisor registers
    ld      a, 0x80
    out0    (UART1_LCTL), a

    ;; Load divisor from parameter
    ld      a, (ix+6)
    out0    (UART1_BRG_L), a
    ld      a, (ix+7)
    out0    (UART1_BRG_H), a

    ;; 8N1: 8 data bits, no parity, 1 stop bit — clears DLAB
    ld      a, 0x03
    out0    (UART1_LCTL), a

    ;; Enable and reset both FIFOs
    ld      a, 0x07
    out0    (UART1_FCTL), a

    ;; Enable the receive-data-available interrupt.  The transmit-empty
    ;; interrupt stays off until the first write arms it (see
    ;; agonLight2WriteUart1Impl below) - there is nothing to send yet.
    ld      a, UART1_IER_RX_BIT
    out0    (UART1_IER), a

    pop     ix
    ret

;; -- int agonLight2PollUart1Impl(void) ------------------------------------
;;    Pops one byte from the RX ring (filled by uart1Isr).  Returns the byte
;;    in HL, or -1 if the ring is empty.  Non-blocking, as before.
;;
;;    NanoOs's scheduler is PREEMPTIVE (a timer interrupt can force a switch
;;    to a different process between any two instructions here), so this can
;;    genuinely race uart1Isr's RX push, or another process's own call to
;;    this same function, mid read-modify-write of uart1RxTail. di/ei brackets
;;    the whole check-and-pop so a preemption (or the ISR) can't land between
;;    reading which slot is next and committing the new tail.
_agonLight2PollUart1Impl:
    di
    ld      a, (uart1RxTail)
    ld      hl, uart1RxHead
    cp      (hl)
    jr      z, .rxEmpty         ; tail == head -> ring empty

    ;; ld bc,0 (not "ld b,0") to also clear BCU, the 24-bit-mode upper byte
    ;; ld b,0 never touches - leaving it with whatever a prior BC user left
    ;; there would add garbage into this address's high byte below.
    ld      bc, 0
    ld      c, a                ; c = tail (the slot to read)
    ld      hl, uart1RxBuf
    add     hl, bc
    ld      a, (hl)             ; a = the byte
    ld      b, a                ; save it - b is free again once hl is formed

    ld      a, (uart1RxTail)
    inc     a
    and     UART1_RING_MASK
    ld      (uart1RxTail), a
    ei

    ld      hl, 0
    ld      l, b
    ret

.rxEmpty:
    ei
    ld      hl, -1
    ret

;; -- void agonLight2WriteUart1Impl(uint8_t c) ------------------------------
;;    c passed at sp+3.  Pushes the byte onto the TX ring (drained by
;;    uart1Isr) and arms the transmit-empty interrupt.  Spins (cooperative
;;    busy-wait, same contract callers already rely on) if the ring is full.
;;
;;    Same preemption hazard as the poll side, doubled: NanoOs's preemptive
;;    scheduler means TWO DIFFERENT PROCESSES can each be mid-way through
;;    this exact function at once (one gets timer-preempted after reading
;;    uart1TxHead but before storing the advanced value; another runs the
;;    whole function in between) - both would claim the same slot and one
;;    write's byte silently vanishes. di/ei brackets the claim-a-slot step so
;;    only one caller (or the ISR) can be touching the ring's bookkeeping at
;;    a time. The di/ei must NOT wrap the retry spin below: waiting for room
;;    with interrupts off would starve the only thing that ever creates room
;;    (uart1Isr draining a byte), deadlocking forever.
_agonLight2WriteUart1Impl:
    push    ix
    ld      ix, 0
    add     ix, sp

.txRingWait:
    di
    ld      a, (uart1TxHead)
    inc     a
    and     UART1_RING_MASK
    ld      hl, uart1TxTail
    cp      (hl)
    jr      nz, .txRingHasRoom  ; room - fall through still atomic
    ei                          ; full - let uart1Isr run and retry
    jr      .txRingWait

.txRingHasRoom:
    ld      a, (uart1TxHead)    ; a = slot to write (the OLD head)
    ld      bc, 0               ; clears BCU too - see agonLight2PollUart1Impl
    ld      c, a
    ld      hl, uart1TxBuf
    add     hl, bc
    ld      a, (ix+6)
    ld      (hl), a

    ld      a, (uart1TxHead)
    inc     a
    and     UART1_RING_MASK
    ld      (uart1TxHead), a

    ;; Arm the transmit-empty interrupt so uart1Isr resumes draining this
    ;; ring.  Read-modify-write on UART1_IER, which uart1Isr also writes (to
    ;; clear this same bit when the ring runs dry) - already covered by the
    ;; di above; ei releases both guards together.
    in0     a, (UART1_IER)
    or      UART1_IER_TX_BIT
    out0    (UART1_IER), a
    ei

    pop     ix
    ret

;; -- uart1Isr --------------------------------------------------------------
;;    Called (interrupts off) from uart1Tramp in Interrupts.asm.  Drains the
;;    RX FIFO into uart1RxBuf, then - if the transmit holding register is
;;    empty and uart1TxBuf has data - writes one byte to THR.  One byte per
;;    interrupt on the TX side is deliberate simplicity for a first pass, not
;;    a FIFO-depth burst; see the file header.
uart1Isr:
.uart1IsrRxLoop:
    in0     a, (UART1_LSR)
    and     0x01                ; DR - receiver data ready
    jr      z, .uart1IsrTx

    in0     a, (UART1_RBR)      ; also clears DR for this byte
    call    uart1RxPushByte
    jr      .uart1IsrRxLoop

.uart1IsrTx:
    in0     a, (UART1_LSR)
    and     0x20                ; THRE - transmit holding register empty
    jr      z, .uart1IsrDone

    ld      a, (uart1TxHead)
    ld      hl, uart1TxTail
    cp      (hl)
    jr      z, .uart1IsrTxIdle  ; head == tail -> nothing queued

    ld      a, (hl)             ; hl still points at uart1TxTail; a = tail
    ld      bc, 0               ; clears BCU too - see agonLight2PollUart1Impl
    ld      c, a
    ld      hl, uart1TxBuf
    add     hl, bc
    ld      a, (hl)
    out0    (UART1_THR), a      ; also clears THRE

    ld      a, (uart1TxTail)
    inc     a
    and     UART1_RING_MASK
    ld      (uart1TxTail), a

    ;; Re-check emptiness against the ring itself, not THRE: the emulator (and
    ;; quite possibly real silicon) re-evaluates whether to vector into this
    ;; interrupt purely from IER's transmit-empty-enable bit being set, NOT
    ;; from THRE actually being true - so as long as that bit is left on,
    ;; sending the last queued byte still leaves the interrupt condition
    ;; "true" and the CPU immediately re-vectors back in here with nothing
    ;; left to do, over and over, until something clears the bit. That
    ;; something has to be THIS pass, right after the ring goes empty - the
    ;; separate .uart1IsrTxIdle path below only covers being re-entered
    ;; after that already happened, so on its own it always arrives one
    ;; spurious re-entry too late.
    ld      a, (uart1TxHead)
    ld      hl, uart1TxTail
    cp      (hl)
    jr      nz, .uart1IsrDone   ; more queued - leave the interrupt armed

.uart1IsrTxIdle:
    ;; Nothing left to send: disable the transmit-empty interrupt.  No di/ei
    ;; needed here - this IS the interrupt context that owns clearing this
    ;; bit; the impl-side setter is what guards against colliding with this
    ;; write.
    in0     a, (UART1_IER)
    and     0xFD                ; clear bit 1 (UART1_IER_TX_BIT), keep the rest
    out0    (UART1_IER), a

.uart1IsrDone:
    ret

;; -- uart1RxPushByte ---------------------------------------------------
;;    a = received byte on entry.  Pushes onto uart1RxBuf, dropping the byte
;;    if the ring is full (never overwrites unread data).  Clobbers af/bc/hl.
uart1RxPushByte:
    ld      b, a                ; b = byte to store
    ld      a, (uart1RxHead)
    inc     a
    and     UART1_RING_MASK
    ld      hl, uart1RxTail
    cp      (hl)
    ret     z                   ; would collide with tail -> full, drop byte

    ld      a, (uart1RxHead)    ; a = slot to write (the OLD head)
    ld      de, 0               ; clears DEU too - see agonLight2PollUart1Impl;
                                 ; offset in de, not bc - b holds the byte
    ld      e, a
    ld      hl, uart1RxBuf
    add     hl, de
    ld      (hl), b             ; store the byte

    ld      a, (uart1RxHead)
    inc     a
    and     UART1_RING_MASK
    ld      (uart1RxHead), a
    ret
