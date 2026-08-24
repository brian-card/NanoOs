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

;;; @file Ports.asm
;;;
;;; @brief eZ80 assembly support functions for reading and writing from/to ports
;;; on the AgonLight 2.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume adl=1
.text

.global _agonLight2ReadPort
.global _agonLight2WritePort

;; -- int agonLight2ReadPort(uint16_t port) -------------
;;    port register address at sp+3 (low byte), sp+4 (high byte)
_agonLight2ReadPort:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      b, (ix+7)
    ld      c, (ix+6)
    ld      hl, 0
    in      l, (c)

    pop     ix
    ret

;; -- void agonLight2WritePort(uint16_t port, uint8_t c) ---------------------
;;    port register address at sp+3 (low byte), sp+4 (high byte)
;;    c passed at sp+6
_agonLight2WritePort:
    push    ix
    ld      ix, 0
    add     ix, sp

    ld      b, (ix+7)
    ld      c, (ix+6)
    ld      a, (ix+9)
    out     (c), a

    pop     ix
    ret
