///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.11.2026
///
/// @file              AgonKeycodes.h
///
/// @brief             FabGL keycode enum values as transmitted in byte 2 of
///                    the Agon VDP keyboard packet (the "vkey" / "keycode"
///                    field in MOS terminology).
///
/// @details
/// Packet layout reminder (4 bytes after the 0x1B 0x01 header from the VDP):
///   +0  keyascii  ASCII value of key, or 0x00 for non-ASCII keys
///   +1  keymods   Modifier bitmask (see AGON_KEYMOD_* below)
///   +2  keycode   FabGL Keycode
///   +3  keydown   1 = key pressed, 0 = key released
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef AGON_KEYCODES_H
#define AGON_KEYCODES_H

// ------------------------------------------------------------------
// Modifier byte bitmask - transmitted as packet byte +1
// ------------------------------------------------------------------
#define AGON_KEYMOD_CTRL        (1 << 0)
#define AGON_KEYMOD_SHIFT       (1 << 1)
#define AGON_KEYMOD_ALT_LEFT    (1 << 2)
#define AGON_KEYMOD_ALT_RIGHT   (1 << 3)
#define AGON_KEYMOD_CAPS_LOCK   (1 << 4)
#define AGON_KEYMOD_NUM_LOCK    (1 << 5)
#define AGON_KEYMOD_SCROLL_LOCK (1 << 6)
#define AGON_KEYMOD_GUI         (1 << 7)


// ------------------------------------------------------------------
// FabGL Keycode — transmitted as packet byte +2
// ------------------------------------------------------------------
#define KEYCODE_NONE                   0                /* No key / unset                          */

// Digits and keypad digits
#define KEYCODE_SPACE                  1                /* Space bar                               */
#define KEYCODE_0                      2                /* 0                                       */
#define KEYCODE_1                      3                /* 1                                       */
#define KEYCODE_2                      4                /* 2                                       */
#define KEYCODE_3                      5                /* 3                                       */
#define KEYCODE_4                      6                /* 4                                       */
#define KEYCODE_5                      7                /* 5                                       */
#define KEYCODE_6                      8                /* 6                                       */
#define KEYCODE_7                      9                /* 7                                       */
#define KEYCODE_8                      10               /* 8                                       */
#define KEYCODE_9                      11               /* 9                                       */
#define KEYCODE_KEYPAD_0               12               /* Keypad 0                                */
#define KEYCODE_KEYPAD_1               13               /* Keypad 1                                */
#define KEYCODE_KEYPAD_2               14               /* Keypad 2                                */
#define KEYCODE_KEYPAD_3               15               /* Keypad 3                                */
#define KEYCODE_KEYPAD_4               16               /* Keypad 4                                */
#define KEYCODE_KEYPAD_5               17               /* Keypad 5                                */
#define KEYCODE_KEYPAD_6               18               /* Keypad 6                                */
#define KEYCODE_KEYPAD_7               19               /* Keypad 7                                */
#define KEYCODE_KEYPAD_8               20               /* Keypad 8                                */
#define KEYCODE_KEYPAD_9               21               /* Keypad 9                                */

// Lower-case letters
#define KEYCODE_a                      22
#define KEYCODE_b                      23
#define KEYCODE_c                      24
#define KEYCODE_d                      25
#define KEYCODE_e                      26
#define KEYCODE_f                      27
#define KEYCODE_g                      28
#define KEYCODE_h                      29
#define KEYCODE_i                      30
#define KEYCODE_j                      31
#define KEYCODE_k                      32
#define KEYCODE_l                      33
#define KEYCODE_m                      34
#define KEYCODE_n                      35
#define KEYCODE_o                      36
#define KEYCODE_p                      37
#define KEYCODE_q                      38
#define KEYCODE_r                      39
#define KEYCODE_s                      40
#define KEYCODE_t                      41
#define KEYCODE_u                      42
#define KEYCODE_v                      43
#define KEYCODE_w                      44
#define KEYCODE_x                      45
#define KEYCODE_y                      46
#define KEYCODE_z                      47

// Upper-case letters
#define KEYCODE_A                      48
#define KEYCODE_B                      49
#define KEYCODE_C                      50
#define KEYCODE_D                      51
#define KEYCODE_E                      52
#define KEYCODE_F                      53
#define KEYCODE_G                      54
#define KEYCODE_H                      55
#define KEYCODE_I                      56
#define KEYCODE_J                      57
#define KEYCODE_K                      58
#define KEYCODE_L                      59
#define KEYCODE_M                      60
#define KEYCODE_N                      61
#define KEYCODE_O                      62
#define KEYCODE_P                      63
#define KEYCODE_Q                      64
#define KEYCODE_R                      65
#define KEYCODE_S                      66
#define KEYCODE_T                      67
#define KEYCODE_U                      68
#define KEYCODE_V                      69
#define KEYCODE_W                      70
#define KEYCODE_X                      71
#define KEYCODE_Y                      72
#define KEYCODE_Z                      73

// Punctuation and symbols
#define KEYCODE_GRAVEACCENT            74               /* `                                       */
#define KEYCODE_ACUTEACCENT            75               /* ´                                       */
#define KEYCODE_QUOTE                  76               /* '                                       */
#define KEYCODE_QUOTEDBL               77               /* "                                       */
#define KEYCODE_EQUALS                 78               /* =                                       */
#define KEYCODE_MINUS                  79               /* -                                       */
#define KEYCODE_KEYPAD_MINUS           80               /* Keypad -                                */
#define KEYCODE_PLUS                   81               /* +                                       */
#define KEYCODE_KEYPAD_PLUS            82               /* Keypad +                                */
#define KEYCODE_KEYPAD_MULTIPLY        83               /* Keypad *                                */
#define KEYCODE_ASTERISK               84               /* *                                       */
#define KEYCODE_BACKSLASH              85               /* \                                       */
#define KEYCODE_KEYPAD_DIVIDE          86               /* Keypad /                                */
#define KEYCODE_SLASH                  87               /* /                                       */
#define KEYCODE_KEYPAD_PERIOD          88               /* Keypad .                                */
#define KEYCODE_PERIOD                 89               /* .                                       */
#define KEYCODE_COLON                  90               /* :                                       */
#define KEYCODE_COMMA                  91               /* ,                                       */
#define KEYCODE_SEMICOLON              92               /* ;                                       */
#define KEYCODE_AMPERSAND              93               /* &                                       */
#define KEYCODE_VERTICALBAR            94               /* |                                       */
#define KEYCODE_HASH                   95               /* #                                       */
#define KEYCODE_AT                     96               /* @                                       */
#define KEYCODE_CARET                  97               /* ^                                       */
#define KEYCODE_DOLLAR                 98               /* $                                       */
#define KEYCODE_POUND                  99               /* £                                       */
#define KEYCODE_EURO                   100              /* €                                       */
#define KEYCODE_PERCENT                101              /* %                                       */
#define KEYCODE_EXCLAIM                102              /* !                                       */
#define KEYCODE_QUESTION               103              /* ?                                       */
#define KEYCODE_LEFTBRACE              104              /* {                                       */
#define KEYCODE_RIGHTBRACE             105              /* }                                       */
#define KEYCODE_LEFTBRACKET            106              /* [                                       */
#define KEYCODE_RIGHTBRACKET           107              /* ]                                       */
#define KEYCODE_LEFTPAREN              108              /* (                                       */
#define KEYCODE_RIGHTPAREN             109              /* )                                       */
#define KEYCODE_LESS                   110              /* <                                       */
#define KEYCODE_GREATER                111              /* >                                       */
#define KEYCODE_UNDERSCORE             112              /* _                                       */
#define KEYCODE_DEGREE                 113              /* °                                       */
#define KEYCODE_SECTION                114              /* §                                       */
#define KEYCODE_TILDE                  115              /* ~                                       */
#define KEYCODE_NEGATION               116              /* ¬                                       */

// Modifier keys
#define KEYCODE_LSHIFT                 117
#define KEYCODE_RSHIFT                 118
#define KEYCODE_LALT                   119
#define KEYCODE_RALT                   120
#define KEYCODE_LCTRL                  121
#define KEYCODE_RCTRL                  122
#define KEYCODE_LGUI                   123
#define KEYCODE_RGUI                   124

// Control / system keys
#define KEYCODE_ESCAPE                 125
#define KEYCODE_PRINTSCREEN            126
#define KEYCODE_SYSREQ                 127
#define KEYCODE_INSERT                 128
#define KEYCODE_KEYPAD_INSERT          129
#define KEYCODE_DELETE                 130
#define KEYCODE_KEYPAD_DELETE          131
#define KEYCODE_BACKSPACE              132
#define KEYCODE_HOME                   133
#define KEYCODE_KEYPAD_HOME            134
#define KEYCODE_END                    135
#define KEYCODE_KEYPAD_END             136
#define KEYCODE_PAUSE                  137
#define KEYCODE_BREAK                  138              /* Ctrl+Pause                              */
#define KEYCODE_SCROLLLOCK             139
#define KEYCODE_NUMLOCK                140
#define KEYCODE_CAPSLOCK               141
#define KEYCODE_TAB                    142
#define KEYCODE_RETURN                 143
#define KEYCODE_KEYPAD_ENTER           144
#define KEYCODE_APPLICATION            145              /* Menu / Application key                  */

// Page navigation
#define KEYCODE_PAGEUP                 146
#define KEYCODE_KEYPAD_PAGEUP          147
#define KEYCODE_PAGEDOWN               148
#define KEYCODE_KEYPAD_PAGEDOWN        149

// Cursor keys
#define KEYCODE_UP                     150
#define KEYCODE_KEYPAD_UP              151
#define KEYCODE_DOWN                   152
#define KEYCODE_KEYPAD_DOWN            153
#define KEYCODE_LEFT                   154
#define KEYCODE_KEYPAD_LEFT            155
#define KEYCODE_RIGHT                  156
#define KEYCODE_KEYPAD_RIGHT           157
#define KEYCODE_KEYPAD_CENTER          158              /* Keypad 5 with Num Lock off              */

// Function keys
#define KEYCODE_F1                     159
#define KEYCODE_F2                     160
#define KEYCODE_F3                     161
#define KEYCODE_F4                     162
#define KEYCODE_F5                     163
#define KEYCODE_F6                     164
#define KEYCODE_F7                     165
#define KEYCODE_F8                     166
#define KEYCODE_F9                     167
#define KEYCODE_F10                    168
#define KEYCODE_F11                    169
#define KEYCODE_F12                    170

// Accented characters — lower case grave
#define KEYCODE_GRAVE_a                171              /* à */
#define KEYCODE_GRAVE_e                172              /* è */
#define KEYCODE_GRAVE_i                173              /* ì */
#define KEYCODE_GRAVE_o                174              /* ò */
#define KEYCODE_GRAVE_u                175              /* ù */
#define KEYCODE_GRAVE_y                176              /* ỳ */

// Accented characters — lower case acute
#define KEYCODE_ACUTE_a                177              /* á */
#define KEYCODE_ACUTE_e                178              /* é */
#define KEYCODE_ACUTE_i                179              /* í */
#define KEYCODE_ACUTE_o                180              /* ó */
#define KEYCODE_ACUTE_u                181              /* ú */
#define KEYCODE_ACUTE_y                182              /* ý */

// Accented characters — upper case grave
#define KEYCODE_GRAVE_A                183              /* À */
#define KEYCODE_GRAVE_E                184              /* È */
#define KEYCODE_GRAVE_I                185              /* Ì */
#define KEYCODE_GRAVE_O                186              /* Ò */
#define KEYCODE_GRAVE_U                187              /* Ù */
#define KEYCODE_GRAVE_Y                188              /* Ỳ */

// Accented characters — upper case acute
#define KEYCODE_ACUTE_A                189              /* Á */
#define KEYCODE_ACUTE_E                190              /* É */
#define KEYCODE_ACUTE_I                191              /* Í */
#define KEYCODE_ACUTE_O                192              /* Ó */
#define KEYCODE_ACUTE_U                193              /* Ú */
#define KEYCODE_ACUTE_Y                194              /* Ý */

// Diaeresis (umlaut) — lower case
#define KEYCODE_UMLAUT_a               195              /* ä */
#define KEYCODE_UMLAUT_e               196              /* ë */
#define KEYCODE_UMLAUT_i               197              /* ï */
#define KEYCODE_UMLAUT_o               198              /* ö */
#define KEYCODE_UMLAUT_u               199              /* ü */
#define KEYCODE_UMLAUT_y               200              /* ÿ */

// Diaeresis (umlaut) — upper case
#define KEYCODE_UMLAUT_A               201              /* Ä */
#define KEYCODE_UMLAUT_E               202              /* Ë */
#define KEYCODE_UMLAUT_I               203              /* Ï */
#define KEYCODE_UMLAUT_O               204              /* Ö */
#define KEYCODE_UMLAUT_U               205              /* Ü */
#define KEYCODE_UMLAUT_Y               206              /* Ÿ */

// Circumflex (caret) — lower case
#define KEYCODE_CARET_a                207              /* â */
#define KEYCODE_CARET_e                208              /* ê */
#define KEYCODE_CARET_i                209              /* î */
#define KEYCODE_CARET_o                210              /* ô */
#define KEYCODE_CARET_u                211              /* û */
#define KEYCODE_CARET_y                212              /* ŷ */

// Circumflex (caret) — upper case
#define KEYCODE_CARET_A                213              /* Â */
#define KEYCODE_CARET_E                214              /* Ê */
#define KEYCODE_CARET_I                215              /* Î */
#define KEYCODE_CARET_O                216              /* Ô */
#define KEYCODE_CARET_U                217              /* Û */
#define KEYCODE_CARET_Y                218              /* Ŷ */

// Cedilla
#define KEYCODE_CEDILLA_c              219              /* ç */
#define KEYCODE_CEDILLA_C              220              /* Ç */

// Tilde
#define KEYCODE_TILDE_a                221              /* ã */
#define KEYCODE_TILDE_o                222              /* õ */
#define KEYCODE_TILDE_n                223              /* ñ */
#define KEYCODE_TILDE_A                224              /* Ã */
#define KEYCODE_TILDE_O                225              /* Õ */
#define KEYCODE_TILDE_N                226              /* Ñ */

// Miscellaneous international
#define KEYCODE_UPPER_a                227              /* ª (feminine ordinal indicator)          */
#define KEYCODE_ESZETT                 228              /* ß                                       */
#define KEYCODE_EXCLAIM_INV            229              /* ¡ (inverted exclamation)                */
#define KEYCODE_QUESTION_INV           230              /* ¿ (inverted question mark)              */
#define KEYCODE_INTERPUNCT             231              /* · (middle dot)                          */
#define KEYCODE_DIAERESIS              232              /* ¨                                       */
#define KEYCODE_SQUARE                 233              /* ² (superscript 2)                       */
#define KEYCODE_CURRENCY               234              /* ¤                                       */
#define KEYCODE_MU                     235              /* µ                                       */
#define KEYCODE_aelig                  236              /* æ                                       */
#define KEYCODE_oslash                 237              /* ø                                       */
#define KEYCODE_aring                  238              /* å                                       */
#define KEYCODE_AELIG                  239              /* Æ                                       */
#define KEYCODE_OSLASH                 240              /* Ø                                       */
#define KEYCODE_ARING                  241              /* Å                                       */

// Special: Raw ASCII passthru
#define KEYCODE_ASCII                  242

// Convenience: total number of defined keycodes
#define AGON_KEYCODES_COUNT  (KEYCODE_ASCII + 1)


// ------------------------------------------------------------------
// Keydown values - transmitted as packet byte +3
// ------------------------------------------------------------------
#define KEY_PRESSED  1
#define KEY_RELEASED 0


// ------------------------------------------------------------------
// Character sequences that correspond to the possible keycodes
// ------------------------------------------------------------------
extern const char *const agonKeycodeStrings[256];


#endif // AGON_KEYCODES_H

