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

;;; @file RuntimeStubs.asm
;;;
;;; @brief eZ80 runtime ABI stubs for NanoOs on Agon Light 2.
;;;
;;; @details
;;; These fill gaps between what the LLVM eZ80 code generator emits and what
;;; the current AgDev libagon.a actually provides.
;;;
;;; @note This file was generated with assistance from claude.ai.

.assume ADL=1
.section .text.runtime_stubs,"ax"

; __ftod:  float-to-double conversion.  On the eZ80/AgDev target both float
;          and double are 32-bit (sizeof(double)==4), so the only code path
;          that calls __ftod is the size==6 branch in scanfParseFloat, which
;          is dead code on this target.  A simple RET is sufficient.
;
.global __ftod
__ftod:
    ret

; __snot:  short bitwise-NOT helper.  libagon exports __snot_fast; the LLVM
;          eZ80 backend emits __snot.  Forward to the library implementation.
.global __snot
.extern __snot_fast
__snot:
    jp __snot_fast
