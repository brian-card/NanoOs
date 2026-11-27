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




/// @file Usage.c
///
/// @brief wintest's usage summary, in an overlay of its own to keep it out
/// of main's.

#include "Wintest.h"

/// @fn void* wintestUsage(void *args)
///
/// @brief Print the usage summary for wintest.
///
/// @param args The FILE* to print to.
///
/// @return Always returns the provided pointer.
void* wintestUsage(void *args) {
  FILE *stream = (FILE*) args;
  fputs(
    "Usage: wintest hello|wait|hold|demo\n"
    "       wintest run COMMAND...|@FILE\n"
    "\n"
    "Connect to the window server.\n"
    "  hello  Print its greeting, then disconnect.\n"
    "  wait   Print its greeting, then wait until the window server goes\n"
    "         away.\n"
    "  hold   Wait until the window server goes away, printing nothing.\n"
    "  demo   Open a window with every kind of widget and one with a canvas,\n"
    "         then print each event until the window server goes away.\n"
    "  run    Carry out the commands, or the words of FILE, then print each\n"
    "         event until the window server goes away:\n"
    "    create X,Y,W,H TITLE [layer=L] [flags=F,...]  L is desktop, normal,\n"
    "      topmost or popup; F is plain, notask, fixed, modal, top, bottom,\n"
    "      left or right\n"
    "    title N TITLE   destroy N   state N normal|minimized|maximized\n"
    "    sleep MS   wait MS (print events for MS)   closes (destroy a window\n"
    "    asked to close)   exit\n"
    "    grid X,Y COLSxROWS [session|twice]   put N ROW,COL,ATTR TEXT\n"
    "    clear N TOP,LEFT,BOTTOM,RIGHT,ATTR   scroll N TOP,BOTTOM,COUNT,ATTR\n"
    "    cursor N ROW,COL,SHOWN   reports N 0|1   burst N COUNT   inject\n"
    "    label ID X,Y,W,H left|center|right TEXT   group ID X,Y,W,H TEXT\n"
    "    button ID X,Y,W,H TEXT   toggle|check ID X,Y,W,H 0|1 TEXT\n"
    "    radio ID X,Y,W,H GROUP 0|1 TEXT   field ID X,Y,W,H MAX TEXT\n"
    "    list ID X,Y,W,H SELECTED|none A,B,...   menu ID X,Y,W,H MENUS\n"
    "    canvas ID X,Y,W,H   (each takes flags=F,... and anchor=ltrb; a _\n"
    "    in a text is a space)   window X,Y,W,H TITLE   add N\n"
    "    set N ID PROPERTY VALUE   get N ID text|value   remove N ID\n"
    "    cells N ID COUNT   face N COLOR   rawcanvas N ID HEX   (N may be\n"
    "    ., the window made last)\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);  return args;
}
