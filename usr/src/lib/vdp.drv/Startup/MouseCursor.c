#include <stdint.h>

#define B 0x00,0x00,0x00,0xFF   /* black  (border)      */
#define W 0xFF,0xFF,0xFF,0xFF   /* white  (fill)        */
#define _ 0x00,0x00,0x00,0x00   /* transparent          */

const uint8_t cursorPixels[] = {
 /* 0    1    2    3    4    5    6    7    8    9   10   11  */
    B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,
    B,   W,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,
    B,   W,   W,   W,   W,   W,   B,   B,   B,   B,   _,   _,
    B,   W,   W,   B,   W,   W,   B,   _,   _,   _,   _,   _,
    B,   W,   B,   _,   B,   W,   W,   B,   _,   _,   _,   _,
    B,   B,   _,   _,   B,   W,   W,   B,   _,   _,   _,   _,
    B,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   _,   B,   W,   B,   _,   _,   _,
    _,   _,   _,   _,   _,   _,   _,   B,   _,   _,   _,   _,
};

#undef B
#undef W
#undef _


