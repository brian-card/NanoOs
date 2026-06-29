; runtime_stubs.asm — eZ80 runtime ABI stubs for NanoOs on Agon Light 2.
;
; These fill gaps between what the LLVM eZ80 code generator emits and what
; the current AgDev libagon.a actually provides.
;
; __ftod:  float-to-double conversion.  On the eZ80/AgDev target both float
;          and double are 32-bit (sizeof(double)==4), so the only code path
;          that calls __ftod is the size==6 branch in scanfParseFloat, which
;          is dead code on this target.  A simple RET is sufficient.
;
; __snot:  short bitwise-NOT helper.  libagon exports __snot_fast; the LLVM
;          eZ80 backend emits __snot.  Forward to the library implementation.

    .assume ADL=1
    .section .text.runtime_stubs,"ax"

    .global __ftod
__ftod:
    ret

    .global __snot
    .extern __snot_fast
__snot:
    jp __snot_fast
