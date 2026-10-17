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


;;; @file stdatomic.asm
;;;
;;; @brief eZ80 assembly implementation of the stdatomic.h functions that need
;;; more than a single instruction on the Agon Light 2.

.assume adl=1
.text

.global _atomic_compare_exchange_strong

;; -- bool atomic_compare_exchange_strong(volatile void *object,
;;      void *expected, void *desired) ------------------------------
;;    object at sp+3, expected at sp+6, desired at sp+9.  Interrupts are held
;;    off between the compare and the swap so that no preemption can land
;;    between them, and the caller's interrupt-enable state is restored on
;;    return.  Returns true in A if *object matched *expected and was replaced
;;    by desired, otherwise false with *expected set to *object.
_atomic_compare_exchange_strong:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      a, i            ; side effect: P/V := IEF2 (1 = interrupts were on)
    push    af
    di

    ld      iy, (ix+6)      ; iy = object
    ld      hl, (iy+0)      ; hl = *object
    ld      iy, (ix+9)      ; iy = expected
    ld      de, (iy+0)      ; de = *expected
    or      a
    sbc     hl, de
    jr      nz, .casMismatch

    ld      iy, (ix+6)
    ld      de, (ix+12)
    ld      (iy+0), de      ; *object = desired
    ld      c, 0xff
    jr      .casDone

.casMismatch:
    add     hl, de          ; hl = *object again
    ld      (iy+0), hl      ; *expected = *object
    ld      c, 0x00

.casDone:
    pop     af              ; recover the saved P/V
    jp      po, .casReturn  ; P/V = 0: caller had interrupts off, leave them off
    ei

.casReturn:
    ld      a, c
    pop     ix
    ret
