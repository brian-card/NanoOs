////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file AgonKeycodes.c
///
/// @brief C code for Agon keycodes.

const char *const agonKeycodeStrings[256] = {
  "",                /* KEYCODE_NONE            */
  " ",               /* KEYCODE_SPACE           */
  "0",               /* KEYCODE_0               */
  "1",               /* KEYCODE_1               */
  "2",               /* KEYCODE_2               */
  "3",               /* KEYCODE_3               */
  "4",               /* KEYCODE_4               */
  "5",               /* KEYCODE_5               */
  "6",               /* KEYCODE_6               */
  "7",               /* KEYCODE_7               */
  "8",               /* KEYCODE_8               */
  "9",               /* KEYCODE_9               */
  "0",               /* KEYCODE_KEYPAD_0        */
  "1",               /* KEYCODE_KEYPAD_1        */
  "2",               /* KEYCODE_KEYPAD_2        */
  "3",               /* KEYCODE_KEYPAD_3        */
  "4",               /* KEYCODE_KEYPAD_4        */
  "5",               /* KEYCODE_KEYPAD_5        */
  "6",               /* KEYCODE_KEYPAD_6        */
  "7",               /* KEYCODE_KEYPAD_7        */
  "8",               /* KEYCODE_KEYPAD_8        */
  "9",               /* KEYCODE_KEYPAD_9        */
  "a",               /* KEYCODE_a               */
  "b",               /* KEYCODE_b               */
  "c",               /* KEYCODE_c               */
  "d",               /* KEYCODE_d               */
  "e",               /* KEYCODE_e               */
  "f",               /* KEYCODE_f               */
  "g",               /* KEYCODE_g               */
  "h",               /* KEYCODE_h               */
  "i",               /* KEYCODE_i               */
  "j",               /* KEYCODE_j               */
  "k",               /* KEYCODE_k               */
  "l",               /* KEYCODE_l               */
  "m",               /* KEYCODE_m               */
  "n",               /* KEYCODE_n               */
  "o",               /* KEYCODE_o               */
  "p",               /* KEYCODE_p               */
  "q",               /* KEYCODE_q               */
  "r",               /* KEYCODE_r               */
  "s",               /* KEYCODE_s               */
  "t",               /* KEYCODE_t               */
  "u",               /* KEYCODE_u               */
  "v",               /* KEYCODE_v               */
  "w",               /* KEYCODE_w               */
  "x",               /* KEYCODE_x               */
  "y",               /* KEYCODE_y               */
  "z",               /* KEYCODE_z               */
  "A",               /* KEYCODE_A               */
  "B",               /* KEYCODE_B               */
  "C",               /* KEYCODE_C               */
  "D",               /* KEYCODE_D               */
  "E",               /* KEYCODE_E               */
  "F",               /* KEYCODE_F               */
  "G",               /* KEYCODE_G               */
  "H",               /* KEYCODE_H               */
  "I",               /* KEYCODE_I               */
  "J",               /* KEYCODE_J               */
  "K",               /* KEYCODE_K               */
  "L",               /* KEYCODE_L               */
  "M",               /* KEYCODE_M               */
  "N",               /* KEYCODE_N               */
  "O",               /* KEYCODE_O               */
  "P",               /* KEYCODE_P               */
  "Q",               /* KEYCODE_Q               */
  "R",               /* KEYCODE_R               */
  "S",               /* KEYCODE_S               */
  "T",               /* KEYCODE_T               */
  "U",               /* KEYCODE_U               */
  "V",               /* KEYCODE_V               */
  "W",               /* KEYCODE_W               */
  "X",               /* KEYCODE_X               */
  "Y",               /* KEYCODE_Y               */
  "Z",               /* KEYCODE_Z               */
  "`",               /* KEYCODE_GRAVEACCENT     */
  "\xC2\xB4",        /* KEYCODE_ACUTEACCENT     */
  "'",               /* KEYCODE_QUOTE           */
  "\"",              /* KEYCODE_QUOTEDBL        */
  "=",               /* KEYCODE_EQUALS          */
  "-",               /* KEYCODE_MINUS           */
  "-",               /* KEYCODE_KEYPAD_MINUS    */
  "+",               /* KEYCODE_PLUS            */
  "+",               /* KEYCODE_KEYPAD_PLUS     */
  "*",               /* KEYCODE_KEYPAD_MULTIPLY */
  "*",               /* KEYCODE_ASTERISK        */
  "\\",              /* KEYCODE_BACKSLASH       */
  "/",               /* KEYCODE_KEYPAD_DIVIDE   */
  "/",               /* KEYCODE_SLASH           */
  ".",               /* KEYCODE_KEYPAD_PERIOD   */
  ".",               /* KEYCODE_PERIOD          */
  ":",               /* KEYCODE_COLON           */
  ",",               /* KEYCODE_COMMA           */
  ";",               /* KEYCODE_SEMICOLON       */
  "&",               /* KEYCODE_AMPERSAND       */
  "|",               /* KEYCODE_VERTICALBAR     */
  "#",               /* KEYCODE_HASH            */
  "@",               /* KEYCODE_AT              */
  "^",               /* KEYCODE_CARET           */
  "$",               /* KEYCODE_DOLLAR          */
  "\xC2\xA3",        /* KEYCODE_POUND           */
  "\xE2\x82\xAC",    /* KEYCODE_EURO            */
  "%",               /* KEYCODE_PERCENT         */
  "!",               /* KEYCODE_EXCLAIM         */
  "?",               /* KEYCODE_QUESTION        */
  "{",               /* KEYCODE_LEFTBRACE       */
  "}",               /* KEYCODE_RIGHTBRACE      */
  "[",               /* KEYCODE_LEFTBRACKET     */
  "]",               /* KEYCODE_RIGHTBRACKET    */
  "(",               /* KEYCODE_LEFTPAREN       */
  ")",               /* KEYCODE_RIGHTPAREN      */
  "<",               /* KEYCODE_LESS            */
  ">",               /* KEYCODE_GREATER         */
  "_",               /* KEYCODE_UNDERSCORE      */
  "\xC2\xB0",        /* KEYCODE_DEGREE          */
  "\xC2\xA7",        /* KEYCODE_SECTION         */
  "~",               /* KEYCODE_TILDE           */
  "\xC2\xAC",        /* KEYCODE_NEGATION        */
  "",                /* KEYCODE_LSHIFT          */
  "",                /* KEYCODE_RSHIFT          */
  "",                /* KEYCODE_LALT            */
  "",                /* KEYCODE_RALT            */
  "",                /* KEYCODE_LCTRL           */
  "",                /* KEYCODE_RCTRL           */
  "",                /* KEYCODE_LGUI            */
  "",                /* KEYCODE_RGUI            */
  "\x1B",            /* KEYCODE_ESCAPE          */
  "",                /* KEYCODE_PRINTSCREEN     */
  "",                /* KEYCODE_SYSREQ          */
  "\x1B[2~",         /* KEYCODE_INSERT          */
  "\x1B[2~",         /* KEYCODE_KEYPAD_INSERT   */
  "\x1B[3~",         /* KEYCODE_DELETE          */
  "\x1B[3~",         /* KEYCODE_KEYPAD_DELETE   */
  "\x7F",            /* KEYCODE_BACKSPACE       */
  "\x1B[H",          /* KEYCODE_HOME            */
  "\x1B[H",          /* KEYCODE_KEYPAD_HOME     */
  "\x1B[F",          /* KEYCODE_END             */
  "\x1B[F",          /* KEYCODE_KEYPAD_END      */
  "",                /* KEYCODE_PAUSE           */
  "",                /* KEYCODE_BREAK           */
  "",                /* KEYCODE_SCROLLLOCK      */
  "",                /* KEYCODE_NUMLOCK         */
  "",                /* KEYCODE_CAPSLOCK        */
  "\t",              /* KEYCODE_TAB             */
  "\r",              /* KEYCODE_RETURN          */
  "\r",              /* KEYCODE_KEYPAD_ENTER    */
  "",                /* KEYCODE_APPLICATION     */
  "\x1B[5~",         /* KEYCODE_PAGEUP          */
  "\x1B[5~",         /* KEYCODE_KEYPAD_PAGEUP   */
  "\x1B[6~",         /* KEYCODE_PAGEDOWN        */
  "\x1B[6~",         /* KEYCODE_KEYPAD_PAGEDOWN */
  "\x1B[A",          /* KEYCODE_UP              */
  "\x1B[A",          /* KEYCODE_KEYPAD_UP       */
  "\x1B[B",          /* KEYCODE_DOWN            */
  "\x1B[B",          /* KEYCODE_KEYPAD_DOWN     */
  "\x1B[D",          /* KEYCODE_LEFT            */
  "\x1B[D",          /* KEYCODE_KEYPAD_LEFT     */
  "\x1B[C",          /* KEYCODE_RIGHT           */
  "\x1B[C",          /* KEYCODE_KEYPAD_RIGHT    */
  "",                /* KEYCODE_KEYPAD_CENTER   */
  "\x1BOP",          /* KEYCODE_F1              */
  "\x1BOQ",          /* KEYCODE_F2              */
  "\x1BOR",          /* KEYCODE_F3              */
  "\x1BOS",          /* KEYCODE_F4              */
  "\x1B[15~",        /* KEYCODE_F5              */
  "\x1B[17~",        /* KEYCODE_F6              */
  "\x1B[18~",        /* KEYCODE_F7              */
  "\x1B[19~",        /* KEYCODE_F8              */
  "\x1B[20~",        /* KEYCODE_F9              */
  "\x1B[21~",        /* KEYCODE_F10             */
  "\x1B[23~",        /* KEYCODE_F11             */
  "\x1B[24~",        /* KEYCODE_F12             */
  "\xC3\xA0",        /* KEYCODE_GRAVE_a         */
  "\xC3\xA8",        /* KEYCODE_GRAVE_e         */
  "\xC3\xAC",        /* KEYCODE_GRAVE_i         */
  "\xC3\xB2",        /* KEYCODE_GRAVE_o         */
  "\xC3\xB9",        /* KEYCODE_GRAVE_u         */
  "\xE1\xBB\xB3",    /* KEYCODE_GRAVE_y         */
  "\xC3\xA1",        /* KEYCODE_ACUTE_a         */
  "\xC3\xA9",        /* KEYCODE_ACUTE_e         */
  "\xC3\xAD",        /* KEYCODE_ACUTE_i         */
  "\xC3\xB3",        /* KEYCODE_ACUTE_o         */
  "\xC3\xBA",        /* KEYCODE_ACUTE_u         */
  "\xC3\xBD",        /* KEYCODE_ACUTE_y         */
  "\xC3\x80",        /* KEYCODE_GRAVE_A         */
  "\xC3\x88",        /* KEYCODE_GRAVE_E         */
  "\xC3\x8C",        /* KEYCODE_GRAVE_I         */
  "\xC3\x92",        /* KEYCODE_GRAVE_O         */
  "\xC3\x99",        /* KEYCODE_GRAVE_U         */
  "\xE1\xBB\xB2",    /* KEYCODE_GRAVE_Y         */
  "\xC3\x81",        /* KEYCODE_ACUTE_A         */
  "\xC3\x89",        /* KEYCODE_ACUTE_E         */
  "\xC3\x8D",        /* KEYCODE_ACUTE_I         */
  "\xC3\x93",        /* KEYCODE_ACUTE_O         */
  "\xC3\x9A",        /* KEYCODE_ACUTE_U         */
  "\xC3\x9D",        /* KEYCODE_ACUTE_Y         */
  "\xC3\xA4",        /* KEYCODE_UMLAUT_a        */
  "\xC3\xAB",        /* KEYCODE_UMLAUT_e        */
  "\xC3\xAF",        /* KEYCODE_UMLAUT_i        */
  "\xC3\xB6",        /* KEYCODE_UMLAUT_o        */
  "\xC3\xBC",        /* KEYCODE_UMLAUT_u        */
  "\xC3\xBF",        /* KEYCODE_UMLAUT_y        */
  "\xC3\x84",        /* KEYCODE_UMLAUT_A        */
  "\xC3\x8B",        /* KEYCODE_UMLAUT_E        */
  "\xC3\x8F",        /* KEYCODE_UMLAUT_I        */
  "\xC3\x96",        /* KEYCODE_UMLAUT_O        */
  "\xC3\x9C",        /* KEYCODE_UMLAUT_U        */
  "\xC5\xB8",        /* KEYCODE_UMLAUT_Y        */
  "\xC3\xA2",        /* KEYCODE_CARET_a         */
  "\xC3\xAA",        /* KEYCODE_CARET_e         */
  "\xC3\xAE",        /* KEYCODE_CARET_i         */
  "\xC3\xB4",        /* KEYCODE_CARET_o         */
  "\xC3\xBB",        /* KEYCODE_CARET_u         */
  "\xC5\xB7",        /* KEYCODE_CARET_y         */
  "\xC3\x82",        /* KEYCODE_CARET_A         */
  "\xC3\x8A",        /* KEYCODE_CARET_E         */
  "\xC3\x8E",        /* KEYCODE_CARET_I         */
  "\xC3\x94",        /* KEYCODE_CARET_O         */
  "\xC3\x9B",        /* KEYCODE_CARET_U         */
  "\xC5\xB6",        /* KEYCODE_CARET_Y         */
  "\xC3\xA7",        /* KEYCODE_CEDILLA_c       */
  "\xC3\x87",        /* KEYCODE_CEDILLA_C       */
  "\xC3\xA3",        /* KEYCODE_TILDE_a         */
  "\xC3\xB5",        /* KEYCODE_TILDE_o         */
  "\xC3\xB1",        /* KEYCODE_TILDE_n         */
  "\xC3\x83",        /* KEYCODE_TILDE_A         */
  "\xC3\x95",        /* KEYCODE_TILDE_O         */
  "\xC3\x91",        /* KEYCODE_TILDE_N         */
  "\xC2\xAA",        /* KEYCODE_UPPER_a         */
  "\xC3\x9F",        /* KEYCODE_ESZETT          */
  "\xC2\xA1",        /* KEYCODE_EXCLAIM_INV     */
  "\xC2\xBF",        /* KEYCODE_QUESTION_INV    */
  "\xC2\xB7",        /* KEYCODE_INTERPUNCT      */
  "\xC2\xA8",        /* KEYCODE_DIAERESIS       */
  "\xC2\xB2",        /* KEYCODE_SQUARE          */
  "\xC2\xA4",        /* KEYCODE_CURRENCY        */
  "\xC2\xB5",        /* KEYCODE_MU              */
  "\xC3\xA6",        /* KEYCODE_aelig           */
  "\xC3\xB8",        /* KEYCODE_oslash          */
  "\xC3\xA5",        /* KEYCODE_aring           */
  "\xC3\x86",        /* KEYCODE_AELIG           */
  "\xC3\x98",        /* KEYCODE_OSLASH          */
  "\xC3\x85",        /* KEYCODE_ARING           */
  "",                /* KEYCODE_ASCII           */
  "",                /* 243                     */
  "",                /* 244                     */
  "",                /* 245                     */
  "",                /* 246                     */
  "",                /* 247                     */
  "",                /* 248                     */
  "",                /* 249                     */
  "",                /* 250                     */
  "",                /* 251                     */
  "",                /* 252                     */
  "",                /* 253                     */
  "",                /* 254                     */
  "",                /* 255                     */
};

