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
/// @brief Command line editing for the MUSH shell:  cursor movement, history
/// kept in a file, and in-place insertion and deletion, with the console in
/// raw mode.

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

/// @def HISTORY_FILE
///
/// @brief The name of the history file, appended to HOME.
#define HISTORY_FILE "/.mush_history"

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
/// @param historyPath The path of the history file, or NULL if there is none.
/// @param historyStart The offset in the history file of the entry shown, or
///   -1 if the line being typed is shown.
/// @param historyEnd The offset in the history file just past the entry shown.
typedef struct LineEditor {
  LineEditorArgs *args;
  int             length;
  int             cursor;
  char           *historyPath;
  long            historyStart;
  long            historyEnd;
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

/// @fn static void lineEditorShow(LineEditor *editor)
///
/// @brief Replace the line on the screen with the contents of the line buffer
/// and put the cursor at its end.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorShow(LineEditor *editor) {
  int oldLength = editor->length;
  lineEditorRepeat('\b', editor->cursor);
  editor->length = (int) strlen(editor->args->line);
  lineEditorPut(editor->args->line, editor->length);
  if (oldLength > editor->length) {
    lineEditorRepeat(' ', oldLength - editor->length);
    lineEditorRepeat('\b', oldLength - editor->length);
  }
  editor->cursor = editor->length;
}

/// @fn static FILE* lineEditorOpenHistory(LineEditor *editor, long *fileSize)
///
/// @brief Open the history file for reading.
///
/// @param editor A pointer to the LineEditor.
/// @param fileSize A pointer to the long that receives the file's size.
///
/// @return Returns the open FILE on success, NULL if there's no history.
static FILE* lineEditorOpenHistory(LineEditor *editor, long *fileSize) {
  if (editor->historyPath == NULL) {
    return NULL;
  }
  FILE *historyFile = fopen(editor->historyPath, "r");
  if (historyFile == NULL) {
    return NULL;
  }
  if (fseek(historyFile, 0, SEEK_END) != 0) {
    fclose(historyFile);
    return NULL;
  }
  *fileSize = ftell(historyFile);
  return historyFile;
}

/// @fn static void lineEditorPreviousEntry(LineEditor *editor)
///
/// @brief Show the history entry before the one shown, or the newest entry if
/// the line being typed is shown.  An entry and its newline fit in the line
/// buffer, so one read that ends where the shown entry starts holds it.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorPreviousEntry(LineEditor *editor) {
  long fileSize = 0;
  FILE *historyFile = lineEditorOpenHistory(editor, &fileSize);
  if (historyFile == NULL) {
    return;
  }

  char *line = editor->args->line;
  long end = (editor->historyStart < 0) ? fileSize : editor->historyStart;
  long start = end - editor->args->size;
  if (start < 0) {
    start = 0;
  }
  int numBytes = 0;
  if ((end > 0) && (fseek(historyFile, start, SEEK_SET) == 0)) {
    numBytes = (int) fread(line, 1, (size_t) (end - start), historyFile);
  }
  fclose(historyFile);
  if ((numBytes <= 0) || (numBytes != end - start)) {
    return;
  }

  line[numBytes - 1] = '\0';
  char *entry = strrchr(line, '\n');
  entry = (entry != NULL) ? (entry + 1) : line;
  editor->historyStart = start + (entry - line);
  editor->historyEnd = end;
  memmove(line, entry, strlen(entry) + 1);
  lineEditorShow(editor);
}

/// @fn static void lineEditorNextEntry(LineEditor *editor)
///
/// @brief Show the history entry after the one shown, or an empty line after
/// the newest entry.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorNextEntry(LineEditor *editor) {
  if (editor->historyStart < 0) {
    return;
  }

  char *line = editor->args->line;
  long fileSize = 0;
  FILE *historyFile = lineEditorOpenHistory(editor, &fileSize);
  int numBytes = 0;
  if ((historyFile != NULL) && (editor->historyEnd < fileSize)
    && (fseek(historyFile, editor->historyEnd, SEEK_SET) == 0)
  ) {
    numBytes = (int) fread(line, 1, (size_t) editor->args->size, historyFile);
  }
  if (historyFile != NULL) {
    fclose(historyFile);
  }

  int entryLength = 0;
  while ((entryLength < numBytes) && (line[entryLength] != '\n')) {
    entryLength++;
  }
  if (entryLength == numBytes) {
    line[0] = '\0';
    editor->historyStart = -1;
  } else {
    line[entryLength] = '\0';
    editor->historyStart = editor->historyEnd;
    editor->historyEnd += entryLength + 1;
  }
  lineEditorShow(editor);
}

/// @fn static bool lineEditorIsNewestEntry(LineEditor *editor)
///
/// @brief Determine whether the line is the same as the newest history entry.
///
/// @param editor A pointer to the LineEditor.
///
/// @return Returns true if it is, false otherwise.
static bool lineEditorIsNewestEntry(LineEditor *editor) {
  long fileSize = 0;
  FILE *historyFile = lineEditorOpenHistory(editor, &fileSize);
  if (historyFile == NULL) {
    return false;
  }

  // The newest entry is the line and a newline, after the previous entry's
  // newline unless it's the only entry.
  long start = fileSize - editor->length - 1;
  int compared = (start > 0) ? -1 : 0;
  bool same = (start >= 0)
    && (fseek(historyFile, start + compared, SEEK_SET) == 0);
  char chunk[8];
  while (same && (compared <= editor->length)) {
    int numBytes = (int) fread(chunk, 1, sizeof(chunk), historyFile);
    if (numBytes <= 0) {
      same = false;
    }
    for (int ii = 0;
      same && (ii < numBytes) && (compared <= editor->length);
      ii++, compared++
    ) {
      char expected = ((compared < 0) || (compared == editor->length))
        ? '\n' : editor->args->line[compared];
      same = (chunk[ii] == expected);
    }
  }
  fclose(historyFile);

  return same;
}

/// @fn static void lineEditorAddEntry(LineEditor *editor)
///
/// @brief Append the line to the history file unless it's empty or the same
/// as the newest entry.
///
/// @param editor A pointer to the LineEditor.
///
/// @return This function returns no value.
static void lineEditorAddEntry(LineEditor *editor) {
  if ((editor->historyPath == NULL) || (editor->length == 0)
    || lineEditorIsNewestEntry(editor)
  ) {
    return;
  }

  FILE *historyFile = fopen(editor->historyPath, "a");
  if (historyFile != NULL) {
    editor->args->line[editor->length] = '\n';
    fwrite(editor->args->line, 1, (size_t) editor->length + 1, historyFile);
    editor->args->line[editor->length] = '\0';
    fclose(historyFile);
  }
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
  if (key == KEY_UP) {
    lineEditorPreviousEntry(editor);
  } else if (key == KEY_DOWN) {
    lineEditorNextEntry(editor);
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
  char input[8];

  while (1) {
    int numBytes = (int) fread(input, 1, sizeof(input), stdin);
    if (numBytes == 0) {
      return -1;
    }

    for (int ii = 0; ii < numBytes; ii++) {
      char c = input[ii];
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
        lineEditorPut("^C\n", 3);
        args->line[0] = '\0';
        editor->length = 0;
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
/// user edit it and recall earlier ones from the history file in HOME.  The
/// console is in raw mode while the line is read and in its previous mode
/// afterward.  An entered line is added to the history file.
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
    .historyPath = NULL,
    .historyStart = -1,
    .historyEnd = 0,
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

  char *home = getenv("HOME");
  if (home != NULL) {
    editor.historyPath = (char*) malloc(strlen(home) + sizeof(HISTORY_FILE));
    if (editor.historyPath != NULL) {
      strcpy(editor.historyPath, home);
      strcat(editor.historyPath, HISTORY_FILE);
    }
  }

  lineEditorArgs->returnValue = lineEditorRead(&editor);
  if (lineEditorArgs->returnValue == 0) {
    lineEditorAddEntry(&editor);
  }
  free(editor.historyPath);

  termios.c_lflag = localModes;
  tcsetattr(STDIN_FILENO, TCSANOW, &termios);
  return args;
}
