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


/// @file LineEditor.c
///
/// @brief Command line editing for the MUSH shell:  cursor movement, history,
/// and in-place insertion and deletion, with the console in raw mode.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "mush.h"

/// @def ESCAPE
///
/// @brief ASCII <escape> character.
#define ESCAPE 0x1b

/// @def CTRL_C
///
/// @brief The byte Ctrl-C sends.
#define CTRL_C 0x03

/// @enum LineEditorKey
///
/// @brief The editing keys an escape sequence can name.
typedef enum LineEditorKey {
  KEY_NONE,
  KEY_UP,
  KEY_DOWN,
  KEY_RIGHT,
  KEY_LEFT,
  KEY_HOME,
  KEY_END,
  KEY_DELETE,
} LineEditorKey;

/// @struct LineEditor
///
/// @brief The state of the line being edited.
///
/// @param args The LineEditorArgs readLine was called with.
/// @param length The number of characters in the line.
/// @param cursor The index in the line of the character under the cursor.
/// @param showingHistory Whether the line and history buffers have been
///   swapped to show the previous command.
typedef struct LineEditor {
  LineEditorArgs *args;
  int             length;
  int             cursor;
  bool            showingHistory;
} LineEditor;

/// @fn static void lineEditorPut(const char *text, int length)
///
/// @brief Write characters to stdout.
///
/// @param text The characters to write.
/// @param length The number of characters to write.
///
/// @return This function returns no value.
static void lineEditorPut(const char *text, int length) {
  if (length > 0) {
    fwrite(text, 1, (size_t) length, stdout);
  }
}

/// @fn static void lineEditorRepeat(char c, int count)
///
/// @brief Write a character to stdout a number of times.
///
/// @param c The character to write.
/// @param count The number of times to write it.
///
/// @return This function returns no value.
static void lineEditorRepeat(char c, int count) {
  char chunk[8];
  memset(chunk, c, sizeof(chunk));
  while (count > 0) {
    int length = (count < (int) sizeof(chunk)) ? count : (int) sizeof(chunk);
    lineEditorPut(chunk, length);
    count -= length;
  }
}

/// @fn static void lineEditorRedrawTail(LineEditor *editor, int erase)
///
/// @brief Redraw the line from the cursor to its end, blank out characters
/// that were past its old end, and put the cursor back.
///
/// @param editor A pointer to the LineEditor.
/// @param erase The number of characters past the end of the line to blank.
///
/// @return This function returns no value.
static void lineEditorRedrawTail(LineEditor *editor, int erase) {
  int tail = editor->length - editor->cursor;
  lineEditorPut(&editor->args->line[editor->cursor], tail);
  lineEditorRepeat(' ', erase);
  lineEditorRepeat('\b', tail + erase);
}

/// @fn static void lineEditorInsert(LineEditor *editor, char c)
///
/// @brief Insert a character at the cursor, if the line has room for it.
///
/// @param editor A pointer to the LineEditor.
/// @param c The character to insert.
///
/// @return This function returns no value.
static void lineEditorInsert(LineEditor *editor, char c) {
  char *line = editor->args->line;
  if (editor->length >= editor->args->size - 1) {
    return;
  }

  memmove(&line[editor->cursor + 1], &line[editor->cursor],
    (size_t) (editor->length - editor->cursor + 1));
  line[editor->cursor] = c;
  editor->length++;
  lineEditorPut(&line[editor->cursor], 1);
  editor->cursor++;
  lineEditorRedrawTail(editor, 0);
}

/// @fn static void lineEditorDelete(LineEditor *editor)
///
/// @brief Delete the character under the cursor.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorDelete(LineEditor *editor) {
  char *line = editor->args->line;
  if (editor->cursor >= editor->length) {
    return;
  }

  memmove(&line[editor->cursor], &line[editor->cursor + 1],
    (size_t) (editor->length - editor->cursor));
  editor->length--;
  lineEditorRedrawTail(editor, 1);
}

/// @fn static void lineEditorSwapHistory(LineEditor *editor)
///
/// @brief Exchange the line with the history buffer and show the result.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorSwapHistory(LineEditor *editor) {
  char *line = editor->args->line;
  char *history = editor->args->history;
  for (int ii = 0; ii < editor->args->size; ii++) {
    char c = line[ii];
    line[ii] = history[ii];
    history[ii] = c;
  }
  editor->showingHistory = !editor->showingHistory;

  int oldLength = editor->length;
  lineEditorRepeat('\b', editor->cursor);
  editor->length = (int) strlen(line);
  lineEditorPut(line, editor->length);
  if (oldLength > editor->length) {
    lineEditorRepeat(' ', oldLength - editor->length);
    lineEditorRepeat('\b', oldLength - editor->length);
  }
  editor->cursor = editor->length;
}

/// @fn static void lineEditorKey(LineEditor *editor, LineEditorKey key)
///
/// @brief Act on an editing key.
///
/// @param editor A pointer to the LineEditor.
/// @param key The LineEditorKey that was pressed.
///
/// @return This function returns no value.
static void lineEditorKey(LineEditor *editor, LineEditorKey key) {
  if ((key == KEY_UP) && !editor->showingHistory
    && (editor->args->history[0] != '\0')
  ) {
    lineEditorSwapHistory(editor);
  } else if ((key == KEY_DOWN) && editor->showingHistory) {
    lineEditorSwapHistory(editor);
  } else if ((key == KEY_LEFT) && (editor->cursor > 0)) {
    lineEditorPut("\b", 1);
    editor->cursor--;
  } else if ((key == KEY_RIGHT) && (editor->cursor < editor->length)) {
    lineEditorPut(&editor->args->line[editor->cursor], 1);
    editor->cursor++;
  } else if (key == KEY_HOME) {
    lineEditorRepeat('\b', editor->cursor);
    editor->cursor = 0;
  } else if (key == KEY_END) {
    lineEditorPut(&editor->args->line[editor->cursor],
      editor->length - editor->cursor);
    editor->cursor = editor->length;
  } else if (key == KEY_DELETE) {
    lineEditorDelete(editor);
  }
}

/// @fn static LineEditorKey lineEditorSequenceKey(char final, int parameter)
///
/// @brief Identify the key an escape sequence names.
///
/// @param final The final byte of the sequence.
/// @param parameter The numeric parameter of a CSI sequence, or 0.
///
/// @return Returns the LineEditorKey, or KEY_NONE for a sequence that isn't
/// an editing key.
static LineEditorKey lineEditorSequenceKey(char final, int parameter) {
  switch (final) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '~':
      if ((parameter == 1) || (parameter == 7)) {
        return KEY_HOME;
      } else if ((parameter == 4) || (parameter == 8)) {
        return KEY_END;
      } else if (parameter == 3) {
        return KEY_DELETE;
      }
      break;
  }

  return KEY_NONE;
}

/// @fn static int lineEditorRead(LineEditor *editor)
///
/// @brief Read and edit input until the line is complete.
///
/// @param editor A pointer to the LineEditor.
///
/// @return Returns 0 when the line is complete, -1 if stdin was closed.
static int lineEditorRead(LineEditor *editor) {
  LineEditorArgs *args = editor->args;
  int escapeState = 0; // 0 none, 1 after ESC, 2 in CSI, 3 after SS3
  int parameter = 0;

  while (1) {
    int numBytes = (int) fread(args->input, 1, (size_t) args->size, stdin);
    if (numBytes == 0) {
      return -1;
    }

    for (int ii = 0; ii < numBytes; ii++) {
      char c = args->input[ii];
      bool afterReturn = args->afterReturn;
      args->afterReturn = false;

      if (escapeState == 1) {
        escapeState = 0;
        if (c == '[') {
          escapeState = 2;
          parameter = 0;
          continue;
        } else if (c == 'O') {
          escapeState = 3;
          continue;
        }
        // An escape by itself:  take the byte after it on its own.
      } else if (escapeState == 2) {
        if ((c >= '0') && (c <= '9')) {
          parameter = (parameter * 10) + (c - '0');
        } else if ((c >= 0x40) && (c <= 0x7e)) {
          escapeState = 0;
          lineEditorKey(editor, lineEditorSequenceKey(c, parameter));
        }
        continue;
      } else if (escapeState == 3) {
        escapeState = 0;
        lineEditorKey(editor, lineEditorSequenceKey(c, 0));
        continue;
      }

      if (c == ESCAPE) {
        escapeState = 1;
      } else if ((c == '\r') || (c == '\n')) {
        if ((c == '\n') && afterReturn) {
          continue;
        }
        args->afterReturn = (c == '\r');
        lineEditorPut("\n", 1);
        return 0;
      } else if (c == CTRL_C) {
        if (editor->showingHistory) {
          lineEditorSwapHistory(editor);
        }
        lineEditorPut("^C\n", 3);
        args->line[0] = '\0';
        return 0;
      } else if ((c == '\b') || (c == 0x7f)) {
        if (editor->cursor > 0) {
          lineEditorPut("\b", 1);
          editor->cursor--;
          lineEditorDelete(editor);
        }
      } else if ((c >= ' ') && (c < 0x7f)) {
        lineEditorInsert(editor, c);
      }
    }
  }
}

/// @fn static void lineEditorPrompt(char prompt)
///
/// @brief Print the shell's prompt.
///
/// @param prompt The character that ends the prompt.
///
/// @return This function returns no value.
static void lineEditorPrompt(char prompt) {
  char *username = getenv("USER");
  if (username == NULL) {
    username = "<unknown user>";
  }
  char *hostname = getenv("HOSTNAME");
  if (hostname == NULL) {
    hostname = "localhost";
  }
  char *pwd = getenv("PWD");
  if (pwd == NULL) {
    pwd = "<unknown directory>";
  }
  printf("mush %s@%s:%s%c ", username, hostname, pwd, prompt);
}

/// @fn void* readLine(void *args)
///
/// @brief Print the prompt and read a command line from stdin, letting the
/// user edit it and recall the previous one.  The console is in raw mode while
/// the line is read and in its previous mode afterward.  When the line is
/// entered, it becomes the history.
///
/// @param args A pointer to a LineEditorArgs.
///
/// @return Returns the args pointer provided.
void* readLine(void *args) {
  LineEditorArgs *lineEditorArgs = (LineEditorArgs*) args;
  LineEditor editor = {
    .args = lineEditorArgs,
    .length = 0,
    .cursor = 0,
    .showingHistory = false,
  };
  lineEditorArgs->line[0] = '\0';

  struct termios termios;
  if (tcgetattr(STDIN_FILENO, &termios) != 0) {
    // Not a console:  read the line as it comes.
    lineEditorPrompt(lineEditorArgs->prompt);
    char *input = fgets(lineEditorArgs->line, lineEditorArgs->size, stdin);
    lineEditorArgs->returnValue = (input != NULL) ? 0 : -1;
    if (input != NULL) {
      lineEditorArgs->line[strcspn(lineEditorArgs->line, "\r\n")] = '\0';
    }
    return args;
  }
  tcflag_t localModes = termios.c_lflag;
  termios.c_lflag &= ~(ECHO | ICANON | ISIG);
  tcsetattr(STDIN_FILENO, TCSANOW, &termios);
  // Keys typed once the prompt is visible must arrive in raw mode.
  lineEditorPrompt(lineEditorArgs->prompt);

  lineEditorArgs->returnValue = lineEditorRead(&editor);
  if ((lineEditorArgs->returnValue == 0) && (lineEditorArgs->line[0] != '\0')) {
    strcpy(lineEditorArgs->history, lineEditorArgs->line);
  }

  termios.c_lflag = localModes;
  tcsetattr(STDIN_FILENO, TCSANOW, &termios);
  return args;
}
