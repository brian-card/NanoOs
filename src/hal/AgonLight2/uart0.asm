.assume adl=1
    .text

    .global _uart0_init
    .global _uart0_putc
    .global _uart0_getc

;; ── eZ80F92 Port D GPIO registers ──────────────────
PD_DR       .equ 0xA2      ; Port D data register
PD_DDR      .equ 0xA3      ; Port D data direction
PD_ALT1     .equ 0xA4      ; Port D alternate function 1
PD_ALT2     .equ 0xA5      ; Port D alternate function 2

;; ── eZ80F92 UART0 registers (base 0xC0) ────────────
UART0_THR   .equ 0xC0      ; TX holding register
UART0_RBR   .equ 0xC0      ; RX buffer register
UART0_BRG_L .equ 0xC0      ; BRG divisor low  (DLAB=1)
UART0_IER   .equ 0xC1      ; Interrupt enable
UART0_BRG_H .equ 0xC1      ; BRG divisor high (DLAB=1)
UART0_FCTL  .equ 0xC2      ; FIFO control (write)
UART0_LCTL  .equ 0xC3      ; Line control
UART0_MCTL  .equ 0xC4      ; Modem control
UART0_LSR   .equ 0xC5      ; Line status

;; ── uart0_init(uint16_t divisor) ────────────────────
;;    divisor passed at ix+6 (low byte), ix+7 (high byte)
_uart0_init:
    push    ix
    ld      ix, 0
    add     ix, sp

    ;; Configure PD0 (TxD0) and PD1 (RxD0) for UART alternate function
    ;; UART mode = ALT1:0, ALT2:1
    in0     a, (PD_ALT1)
    and     0xFC                ; clear bits 0,1
    out0    (PD_ALT1), a

    in0     a, (PD_ALT2)
    or      0x03                ; set bits 0,1
    out0    (PD_ALT2), a

    ;; Set DDR bits 0,1 to input — peripheral overrides direction
    in0     a, (PD_DDR)
    or      0x03
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

    pop     ix
    ret

;; ── uart0_putc(uint8_t c) ───────────────────────────
;;    c passed at ix+6
_uart0_putc:
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

;; ── int uart0_getc(void) ────────────────────────────
;;    returns received byte in HL, or -1 if no data ready (non-blocking)
_uart0_getc:
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
