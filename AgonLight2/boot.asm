; boot.asm — minimal eZ80F92 bootloader for NanoOS
; Assembles with ez80-asm / ZDS II / fasmg with eZ80 support
;
; The eZ80 reset vector is at 0x000000.
; CPU comes up in Z80 mode (ADL=0), so the first instruction
; must be a .LIL-suffixed jump to enter ADL mode.

    .ASSUME ADL = 0           ; we start in Z80 mode

    ORG   0x000000

; === Reset vector ================================================
reset_vector:
    JP.LIL  _start            ; 4-byte opcode: sets ADL=1, jumps to _start

    ; ----------------------------------------------------------
    ; From here on we're in ADL mode (24-bit addresses/SP)
    ; ----------------------------------------------------------
    .ASSUME ADL = 1

; === NMI (Non-Maskable Interrupt) vector =========================
; Hardwired at 0x000066 in eZ80 silicon. Pad to reach it.
    ORG   0x000066

nmi_handler:
    RETN                      ; restore IFF1 from IFF2 and return

; === Main startup code ===========================================
_start:
    DI                        ; disable maskable interrupts while we set up
    STMIX                     ; set mixed-mode stack frames (3-byte return addresses)

    LD    SP, 0x0C0000        ; top of 512KB external SRAM

    ; --- Zero BSS ------------------------------------------------
    ; Requires linker to define __bss_start, __bss_size.
    ; Skip this block if you're not using .bss yet.

    LD    HL, __bss_start
    LD    BC, __bss_size
    LD    A,  0
    OR    A, C                ; check if BC == 0
    OR    A, B
    JR    Z, .bss_done
.bss_loop:
    LD    (HL), 0
    INC   HL
    DEC   BC
    LD    A, B
    OR    A, C
    JR    NZ, .bss_loop
.bss_done:

    ; --- Call main -----------------------------------------------
    CALL  _main               ; int main(void)

    ; --- Halt if main returns ------------------------------------
.hang:
    HALT
    JR    .hang


    ; Externals (provided by linker script)
    EXTERN __bss_start
    EXTERN __bss_size
    EXTERN _main
