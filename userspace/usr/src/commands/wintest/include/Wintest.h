///////////////////////////////////////////////////////////////////////////////
///
/// @file              Wintest.h
///
/// @brief             What wintest's overlays share:  the session, and what a
///                    call to the Script or Print overlay works on.
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


#ifndef WINTEST_H
#define WINTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NanoOsUtils.h"
#include "../../../src/kernel/Window.h"

/// @struct Wintest
///
/// @brief What wintest's overlays share.
///
/// @param closes Whether a window asked to close is destroyed.
/// @param more Whether Print's caller should go on reading events.
/// @param argc The number of arguments.
/// @param rc The negative errno value Print reports, for an error.
/// @param argv The arguments.
/// @param command The command Print reports an error of, or NULL to print
///   event instead.
/// @param window The window the command named, or NULL.
/// @param event The event Print prints.
/// @param session The connection.
typedef struct Wintest {
  bool        closes;
  bool        more;
  int         argc;
  int         rc;
  char      **argv;
  const char *command;
  const char *window;
  WinEvent    event;
  WinSession  session;
} Wintest;

#endif // WINTEST_H
