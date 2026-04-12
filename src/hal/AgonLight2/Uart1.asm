.assume adl=1
    .text

    .global _uart1_init
    .global _uart1_putc
    .global _uart1_getc

;; ── eZ80F92 Port C GPIO registers ──────────────────
PC_DR       .equ 0x9E      ; Port C data register
PC_DDR      .equ 0x9F      ; Port C data direction (1=input, 0=output)
PC_ALT1     .equ 0xA0      ; Port C alternate function 1
PC_ALT2     .equ 0xA1      ; Port C alternate function 2

;; ── eZ80F92 UART1 registers (base 0xD0) ────────────
UART1_THR   .equ 0xD0      ; TX holding register
UART1_RBR   .equ 0xD0      ; RX buffer register
UART1_BRG_L .equ 0xD0      ; BRG divisor low  (DLAB=1)
UART1_IER   .equ 0xD1      ; Interrupt enable
UART1_BRG_H .equ 0xD1      ; BRG divisor high (DLAB=1)
UART1_FCTL  .equ 0xD2      ; FIFO control (write)
UART1_LCTL  .equ 0xD3      ; Line control
UART1_MCTL  .equ 0xD4      ; Modem control
UART1_LSR   .equ 0xD5      ; Line status

;; ── uart1_init(uint16_t divisor) ────────────────────
;;    divisor passed at ix+6 (low byte), ix+7 (high byte)
_uart1_init:
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

    ;; Disable UART1 interrupts
    xor     a
    out0    (UART1_IER), a

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

    pop     ix
    ret

;; ── uart1_putc(uint8_t c) ───────────────────────────
;;    c passed at ix+6
_uart1_putc:
    push    ix
    ld      ix, 0
    add     ix, sp

.Ltx_wait:
    in0     a, (UART1_LSR)
    and     0x20                ; THRE — transmit holding register empty
    jr      z, .Ltx_wait

    ld      a, (ix+6)
    out0    (UART1_THR), a

    pop     ix
    ret

;; ── int uart1_getc(void) ────────────────────────────
;;    returns received byte in HL, or -1 if no data ready
_uart1_getc:
    in0     a, (UART1_LSR)
    and     0x01                ; DR — data ready
    jr      z, .Lno_char

    in0     a, (UART1_RBR)
    ld      hl, 0
    ld      l, a
    ret

.Lno_char:
    ld      hl, -1
    ret
