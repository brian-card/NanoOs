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




/// @file Demo.c
///
/// @brief wintest demo's script:  a window with every kind of widget, one
/// whose canvas holds graphics 6's cells, and one whose buttons open each
/// kind of dialog.

#include "Wintest.h"

/// @var _demoScript
///
/// @brief The commands wintest demo carries out, as wintest run would.
static const char _demoScript[] =
  "closes "
  "menu 1 0,0,298,12 "
  "&File,10=New:Ctrl+N,11=Open:Ctrl+O,12=Save:Ctrl+S,-,13=Autosave/c,"
  "14=Print/d,-,15=Quit|&Edit,20=Undo/d,21=Cut,22=Copy,23=Paste|"
  "&Help,30=About "
  "label 2 10,22,64,10 left Name: "
  "field 3 80,20,208,14 32 - anchor=ltr "
  "label 4 10,42,64,10 left Password: "
  "field 5 80,40,208,14 16 - flags=password anchor=ltr "
  "check 6 10,62,120,12 1 Remember_me "
  "toggle 7 150,60,80,16 0 Bold "
  "group 8 6,82,286,44 Size anchor=ltr "
  "radio 9 16,100,70,12 1 1 Small "
  "radio 10 96,100,70,12 1 0 Medium "
  "radio 11 186,100,70,12 1 0 Large "
  "label 12 10,134,120,10 left Fruit: "
  "list 13 10,146,180,92 0 Apple,Banana,Cherry,Date,Elderberry,Fig,Grape,"
  "Honeydew,Kiwi,Lemon,Mango,Nectarine anchor=ltb "
  "button 14 200,146,88,16 Disabled flags=disabled anchor=tr "
  "button 15 128,258,76,16 OK flags=default anchor=rb "
  "button 16 212,258,76,16 Cancel flags=cancel anchor=rb "
  "window 8,24,300,300 All_widgets "
  "canvas 1 0,0,320,240 "
  "window 316,24,322,256 Canvas "
  "cells . 1 192 "
  "button 1 10,10,140,16 Message... button 2 10,32,140,16 Input... "
  "button 3 10,54,140,16 Open... button 4 10,76,140,16 Save... "
  "button 5 10,98,140,16 Find_(own_dialog)... "
  "window 316,288,160,140 Dialogs";

/// @var _dialogCommands
///
/// @brief What each button of the Dialogs window does, as wintest run's
/// commands, with %u for the Dialogs window, which owns the dialog.  Find
/// is an application's own dialog whose Browse... opens a file dialog over
/// it; its field and box are read back once it ends.
static const char *const _dialogCommands[] = {
  "message %u question yesnocancel Message "
    "Save_the_changes_to_the_document_before_closing_it?",
  "input %u Input Your_name: Nook",
  "open %u -",
  "save %u /tmp/untitled.txt",
  "label 1 10,10,150,10 left Find_what: field 2 10,24,150,14 32 - "
    "check 3 10,46,150,12 0 Match_case "
    "button 4 170,8,74,16 Find flags=default "
    "button 5 170,28,74,16 Browse... "
    "button 6 170,48,74,16 Cancel flags=cancel "
    "dialog %u 0,0,262,100 Find browse=5 "
    "get . 2 text get . 3 value destroy .",
};

/// @fn void* wintestDemo(void *args)
///
/// @brief Copy wintest demo's script to the heap, since this overlay's
/// memory is another's once it returns.
///
/// @param args A pointer to the char* to store the copy in, which is NULL if
///   there was no memory.
///
/// @return Always returns the provided pointer.
void* wintestDemo(void *args) {
  char **text = (char**) args;
  *text = (char*) malloc(sizeof(_demoScript));
  if (*text != NULL) {
    memcpy(*text, _demoScript, sizeof(_demoScript));
  }

  return args;
}

/// @fn void* wintestDemoCommands(void *args)
///
/// @brief Make the commands of the Dialogs window's button pressed the
/// context's arguments, on the heap, for wintest demo.
///
/// @param args A pointer to the Wintest, whose demoButton is set, and whose
///   argc, argv, demoText and demoWords are set, demoWords NULL if there was
///   no memory or no such button.
///
/// @return Always returns the provided pointer.
void* wintestDemoCommands(void *args) {
  Wintest *wintest = (Wintest*) args;
  uint8_t button = wintest->demoButton;

  wintest->demoText = NULL;
  wintest->demoWords = NULL;

  if ((button == 0) || (button > WINTEST_COUNT(_dialogCommands))) {
    return args;
  }

  const char *format = _dialogCommands[button - 1];
  size_t size = strlen(format) + 4;
  char *text = (char*) malloc(size);
  int count = WINTEST_SCRIPT_START;

  if (text == NULL) {
    return args;
  }

  snprintf(text, size, format, (unsigned int) wintest->demoWindow);

  for (const char *word = text; *word != '\0'; ) {
    word += strspn(word, " ");

    if (*word != '\0') {
      count++;
      word += strcspn(word, " ");
    }
  }

  char **words = (char**) calloc((size_t) count, sizeof(char*));
  if (words == NULL) {
    free(text);
    return args;
  }

  count = WINTEST_SCRIPT_START;

  for (char *word = text; *(word += strspn(word, " ")) != '\0'; ) {
    words[count++] = word;
    word += strcspn(word, " ");

    if (*word != '\0') {
      *word++ = '\0';
    }
  }

  words[0] = wintest->argv[0];
  words[1] = wintest->argv[1];
  wintest->demoText = text;
  wintest->demoWords = words;
  wintest->argc = count;
  wintest->argv = words;

  return args;
}
