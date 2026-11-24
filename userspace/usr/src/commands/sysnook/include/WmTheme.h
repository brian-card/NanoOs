///////////////////////////////////////////////////////////////////////////////
///
/// @file              WmTheme.h
///
/// @brief             Nook's look:  the sizes, in pixels with the 6x8 font,
///                    and the colours of everything the window server draws.
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


#ifndef WM_THEME_H
#define WM_THEME_H

/// @def WM_BORDER
///
/// @brief The width of a decorated window's border.
#define WM_BORDER 1

/// @def WM_TITLE_HEIGHT
///
/// @brief The height of a window's title bar, inside the border.
#define WM_TITLE_HEIGHT 14

/// @def WM_TITLE_INSET
///
/// @brief How far into the title bar its text starts.
#define WM_TITLE_INSET 4

/// @def WM_TITLE_TEXT_TOP
///
/// @brief How far below the top of the title bar its 8-pixel text starts.
#define WM_TITLE_TEXT_TOP 3

/// @def WM_BUTTON_WIDTH
///
/// @brief The width of each caption button, which is as high as the title
/// bar.
#define WM_BUTTON_WIDTH 18

/// @def WM_BUTTONS_WIDTH
///
/// @brief The width of the three caption buttons together.
#define WM_BUTTONS_WIDTH (3 * WM_BUTTON_WIDTH)

/// @def WM_GLYPH_SIZE
///
/// @brief The width and height of a caption button's glyph.
#define WM_GLYPH_SIZE 7

/// @def WM_FRAME_TOP
///
/// @brief How far below a decorated window's top its body starts:  the
/// border and the title bar.
#define WM_FRAME_TOP (WM_BORDER + WM_TITLE_HEIGHT)

/// @def WM_ZONE_INSIDE
///
/// @brief How many pixels of a resize grab zone are inside the frame,
/// counting the border.
#define WM_ZONE_INSIDE 2

/// @def WM_ZONE_OUTSIDE
///
/// @brief How many pixels of a resize grab zone are outside the frame.
#define WM_ZONE_OUTSIDE 2

/// @def WM_ZONE_CORNER
///
/// @brief How far along each side from a corner a grab zone resizes both
/// ways.
#define WM_ZONE_CORNER 10

/// @def WM_MIN_WIDTH
///
/// @brief The narrowest a decorated window can be:  the caption buttons, the
/// title's inset, 4 characters of title, and the border.
#define WM_MIN_WIDTH (WM_BUTTONS_WIDTH + WM_TITLE_INSET + 24 + 2 * WM_BORDER)

/// @def WM_MIN_HEIGHT
///
/// @brief The shortest a decorated window can be:  its border, its title
/// bar, and 16 pixels of body.
#define WM_MIN_HEIGHT (WM_FRAME_TOP + 16 + WM_BORDER)

/// @def WM_KEEP_TITLE
///
/// @brief How much of a moved window's title bar stays on the screen.
#define WM_KEEP_TITLE 32

/// @def WM_DESKTOP
///
/// @brief The colour of the desktop.
#define WM_DESKTOP 0x00AAAAFF

/// @def WM_FACE
///
/// @brief The colour of a window's face.
#define WM_FACE 0xFFFFFFFF

/// @def WM_LIGHT
///
/// @brief The colour of an inactive title bar, and of a caption button's
/// face under the pointer.
#define WM_LIGHT 0xAAAAAAFF

/// @def WM_DARK
///
/// @brief The colour of an inactive border and of inactive text, and of a
/// pressed caption button's face.
#define WM_DARK 0x555555FF

/// @def WM_TEXT
///
/// @brief The colour of text.
#define WM_TEXT 0x000000FF

/// @def WM_ACCENT
///
/// @brief The colour of an active title bar and border.
#define WM_ACCENT 0x0000AAFF

/// @def WM_ON_ACCENT
///
/// @brief The colour of text on the accent colour.
#define WM_ON_ACCENT 0xFFFFFFFF

/// @def WM_CLOSE_HOT
///
/// @brief The colour of the close button's face under the pointer.
#define WM_CLOSE_HOT 0xFF5555FF

#endif // WM_THEME_H
