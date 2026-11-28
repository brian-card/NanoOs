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


/// @file Make.c
///
/// @brief Making the templates of the stock dialogs, and the records of a
/// message's icon, entered from the Dialogs overlay once per dialog.

#include "Wm.h"
#include "../../../src/kernel/Window.h"

/// @def WM_DIALOG_MARGIN
///
/// @brief The space between a message or input dialog's body's edges and
/// what is in it, and between its parts.
#define WM_DIALOG_MARGIN 12

/// @def WM_DIALOG_BUTTON_WIDTH
///
/// @brief The width of a stock dialog's buttons.
#define WM_DIALOG_BUTTON_WIDTH 60

/// @def WM_DIALOG_BUTTON_HEIGHT
///
/// @brief The height of a stock dialog's buttons and fields.
#define WM_DIALOG_BUTTON_HEIGHT 14

/// @def WM_DIALOG_BUTTON_GAP
///
/// @brief The space between a stock dialog's buttons.
#define WM_DIALOG_BUTTON_GAP 6

/// @def WM_DIALOG_COLUMNS
///
/// @brief The most columns of a message's lines.
#define WM_DIALOG_COLUMNS 40

/// @def WM_DIALOG_LINES
///
/// @brief The most lines of a message.
#define WM_DIALOG_LINES 10

/// @def WM_DIALOG_FIRST_LINE
///
/// @brief The identifier of a message's first line, a label, the others
/// following.
#define WM_DIALOG_FIRST_LINE 40

/// @def WM_DIALOG_FIELD_WIDTH
///
/// @brief The width of an input dialog's prompt and field.
#define WM_DIALOG_FIELD_WIDTH 240

/// @def WM_FILE_WIDTH
///
/// @brief The width of a file dialog's body.
#define WM_FILE_WIDTH 400

/// @def WM_FILE_HEIGHT
///
/// @brief The height of a file dialog's body.
#define WM_FILE_HEIGHT 220

/// @var _wmButtonResults
///
/// @brief The results of each WinDialogButtons' buttons, left to right,
/// WIN_RESULT_CANCEL ending a set of fewer than three.
static const uint8_t _wmButtonResults[][3] = {
  { WIN_RESULT_OK, WIN_RESULT_CANCEL, WIN_RESULT_CANCEL },
  { WIN_RESULT_OK, WIN_RESULT_CANCEL, WIN_RESULT_CANCEL },
  { WIN_RESULT_YES, WIN_RESULT_NO, WIN_RESULT_CANCEL },
  { WIN_RESULT_YES, WIN_RESULT_NO, WIN_RESULT_CANCEL },
};

/// @var _wmButtonCounts
///
/// @brief The number of each WinDialogButtons' buttons.
static const uint8_t _wmButtonCounts[] = { 1, 2, 2, 3 };

/// @var _wmResultNames
///
/// @brief Each WIN_RESULT_* button's text.
static const char *const _wmResultNames[] = { "Cancel", "OK", "Yes", "No" };

/// @fn static void wmAddButtons(WinTemplate *widgets, uint8_t buttons,
///   int16_t right, int16_t top)
///
/// @brief Add a message's buttons, right-aligned:  the first is the default
/// button, and Cancel, or No with no Cancel, or OK alone, is the cancel
/// button.
///
/// @param widgets The template.
/// @param buttons The WinDialogButtons.
/// @param right Where the last button's right edge goes.
/// @param top Where the buttons' top edges go.
///
/// @return This function returns no value.
static void wmAddButtons(WinTemplate *widgets, uint8_t buttons,
  int16_t right, int16_t top
) {
  uint8_t count = _wmButtonCounts[buttons];
  int16_t left = right - (count * WM_DIALOG_BUTTON_WIDTH)
    - ((count - 1) * WM_DIALOG_BUTTON_GAP);

  for (uint8_t ii = 0; ii < count; ii++) {
    uint8_t result = _wmButtonResults[buttons][ii];
    uint8_t flags = (ii == 0) ? WIN_WIDGET_DEFAULT : 0;

    if ((ii == (count - 1)) && ((result != WIN_RESULT_YES) || (count == 1))) {
      flags |= WIN_WIDGET_CANCEL;
    }

    GfxRect rect = { left, top, WM_DIALOG_BUTTON_WIDTH,
      WM_DIALOG_BUTTON_HEIGHT };
    winAddButton(widgets, (uint8_t) (WM_DIALOG_BUTTONS + result), flags, 0,
      rect, _wmResultNames[result]);
    left += WM_DIALOG_BUTTON_WIDTH + WM_DIALOG_BUTTON_GAP;
  }
}

/// @fn static uint8_t wmWrap(const WmDialogText *text, uint8_t *starts,
///   uint8_t *lengths, bool *cut)
///
/// @brief Break a message into lines at word boundaries, each at most
/// WM_DIALOG_COLUMNS, at a newline, and inside a word longer than a line,
/// keeping at most WM_DIALOG_LINES.
///
/// @param text The message.
/// @param starts Where each line starts.
/// @param lengths Each line's length.
/// @param cut Where to store whether text was left over.
///
/// @return Returns the number of lines.
static uint8_t wmWrap(const WmDialogText *text, uint8_t *starts,
  uint8_t *lengths, bool *cut
) {
  uint8_t at = 0;
  uint8_t count = 0;

  while ((at < text->length) && (count < WM_DIALOG_LINES)) {
    uint8_t end = at;
    uint8_t space = 0;

    while ((end < text->length) && (text->text[end] != '\n')
      && ((end - at) < WM_DIALOG_COLUMNS)
    ) {
      if (text->text[end] == ' ') {
        space = end;
      }

      end++;
    }

    uint8_t next = end;

    if ((end < text->length) && (text->text[end] != '\n')
      && (text->text[end] != ' ') && (space > at)
    ) {
      end = space;
      next = space;
    }

    starts[count] = at;
    lengths[count] = (uint8_t) (end - at);
    count++;

    if ((next < text->length) && ((text->text[next] == '\n')
      || (text->text[next] == ' '))
    ) {
      next++;
    }

    at = next;
  }

  *cut = (at < text->length);

  return count;
}

/// @fn static void wmMessageTemplate(WmState *state,
///   const WmDialogOpen *open, WinTemplate *widgets, GfxRect *frame)
///
/// @brief Make a message's template:  the icon on the left, the lines of
/// text, and the buttons under them, and the frame that holds them.
///
/// @param state The window manager's state.
/// @param open The dialog.
/// @param widgets The template.
/// @param frame Where to store the frame's size.
///
/// @return This function returns no value.
static void wmMessageTemplate(WmState *state, const WmDialogOpen *open,
  WinTemplate *widgets, GfxRect *frame
) {
  uint8_t glyphW = state->display.builtInFont.glyphW;
  int16_t lineHeight = state->display.builtInFont.glyphH + 4;
  int16_t left = WM_DIALOG_MARGIN;
  uint8_t starts[WM_DIALOG_LINES];
  uint8_t lengths[WM_DIALOG_LINES];
  bool cut = false;
  uint8_t count = wmWrap(&open->parts[1], starts, lengths, &cut);
  int16_t height = (int16_t) (count * lineHeight);
  uint8_t widest = 0;

  if (open->icon != WIN_ICON_NONE) {
    GfxRect rect = { WM_DIALOG_MARGIN, WM_DIALOG_MARGIN, WM_DIALOG_ICON_SIZE,
      WM_DIALOG_ICON_SIZE };
    winAddCanvas(widgets, WM_DIALOG_ICON, 0, 0, rect);
    left += WM_DIALOG_ICON_SIZE + WM_DIALOG_MARGIN;

    if (height < WM_DIALOG_ICON_SIZE) {
      height = WM_DIALOG_ICON_SIZE;
    }
  }

  for (uint8_t ii = 0; ii < count; ii++) {
    char line[WM_DIALOG_COLUMNS + 1];
    uint8_t length = lengths[ii];

    memcpy(line, &open->parts[1].text[starts[ii]], length);

    if (cut && (ii == (count - 1))) {
      if (length > (WM_DIALOG_COLUMNS - 3)) {
        length = WM_DIALOG_COLUMNS - 3;
      }

      memcpy(&line[length], "...", 3);
      length += 3;
    }

    line[length] = '\0';

    if (length > widest) {
      widest = length;
    }

    GfxRect rect = { left, (int16_t) (WM_DIALOG_MARGIN + (ii * lineHeight)),
      (int16_t) (length * glyphW), lineHeight };
    winAddLabel(widgets, (uint8_t) (WM_DIALOG_FIRST_LINE + ii), 0, 0, rect,
      WIN_ALIGN_LEFT, line);
  }

  uint8_t buttons = _wmButtonCounts[open->buttons];
  int16_t width = left + (widest * glyphW) + WM_DIALOG_MARGIN;
  int16_t needed = (int16_t) ((2 * WM_DIALOG_MARGIN)
    + (buttons * WM_DIALOG_BUTTON_WIDTH)
    + ((buttons - 1) * WM_DIALOG_BUTTON_GAP));

  if (width < needed) {
    width = needed;
  }
  if (width < (WM_MIN_WIDTH - (2 * WM_BORDER))) {
    width = WM_MIN_WIDTH - (2 * WM_BORDER);
  }

  int16_t top = WM_DIALOG_MARGIN + height + WM_DIALOG_MARGIN;
  wmAddButtons(widgets, open->buttons, width - WM_DIALOG_MARGIN, top);
  frame->w = width + (2 * WM_BORDER);
  frame->h = top + WM_DIALOG_BUTTON_HEIGHT + WM_DIALOG_MARGIN + WM_FRAME_TOP
    + WM_BORDER;
}

/// @fn static void wmInputTemplate(WmState *state,
///   const WmDialogOpen *open, WinTemplate *widgets, GfxRect *frame,
///   char *text)
///
/// @brief Make an input dialog's template:  the prompt, the field with the
/// initial text, OK and Cancel, and the frame that holds them.
///
/// @param state The window manager's state.
/// @param open The dialog.
/// @param widgets The template.
/// @param frame Where to store the frame's size.
/// @param text A buffer of at least 256 bytes for the texts.
///
/// @return This function returns no value.
static void wmInputTemplate(WmState *state, const WmDialogOpen *open,
  WinTemplate *widgets, GfxRect *frame, char *text
) {
  int16_t lineHeight = state->display.builtInFont.glyphH + 4;
  uint8_t length = open->parts[1].length;
  uint8_t columns = WM_DIALOG_FIELD_WIDTH / state->display.builtInFont.glyphW;

  if (length > columns) {
    length = columns;
  }

  memcpy(text, open->parts[1].text, length);
  text[length] = '\0';
  GfxRect prompt = { WM_DIALOG_MARGIN, WM_DIALOG_MARGIN, WM_DIALOG_FIELD_WIDTH,
    lineHeight };
  winAddLabel(widgets, WM_DIALOG_FIRST_LINE, 0, 0, prompt, WIN_ALIGN_LEFT,
    text);

  length = open->parts[2].length;

  if (length > open->maximum) {
    length = open->maximum;
  }

  memcpy(text, open->parts[2].text, length);
  text[length] = '\0';
  GfxRect field = { WM_DIALOG_MARGIN, WM_DIALOG_MARGIN + lineHeight + 4,
    WM_DIALOG_FIELD_WIDTH, WM_DIALOG_BUTTON_HEIGHT };
  winAddTextField(widgets, WIN_DIALOG_FIELD, 0, 0, field, open->maximum,
    text);

  int16_t width = WM_DIALOG_FIELD_WIDTH + (2 * WM_DIALOG_MARGIN);
  int16_t top = field.y + field.h + WM_DIALOG_MARGIN;

  wmAddButtons(widgets, WIN_BUTTONS_OK_CANCEL, width - WM_DIALOG_MARGIN, top);
  frame->w = width + (2 * WM_BORDER);
  frame->h = top + WM_DIALOG_BUTTON_HEIGHT + WM_DIALOG_MARGIN + WM_FRAME_TOP
    + WM_BORDER;
}

/// @fn static void wmFileTemplate(WmState *state, const WmDialogOpen *open,
///   WinTemplate *widgets, GfxRect *frame)
///
/// @brief Make a file dialog's template:  the places, the directory and its
/// entries, the name, Open or Save and Cancel, and the hidden label of the
/// path, in a fixed frame.
///
/// @param state The window manager's state.
/// @param open The dialog.
/// @param widgets The template.
/// @param frame Where to store the frame's size.
///
/// @return This function returns no value.
static void wmFileTemplate(WmState *state, const WmDialogOpen *open,
  WinTemplate *widgets, GfxRect *frame
) {
  int16_t lineHeight = state->display.builtInFont.glyphH + 4;
  GfxRect places = { 8, 8, 100, lineHeight };
  GfxRect lookIn = { 116, 8, 276, lineHeight };
  GfxRect placesList = { 8, 22, 100, 142 };
  GfxRect entries = { 116, 22, 276, 142 };
  GfxRect name = { 8, 172, 40, WM_DIALOG_BUTTON_HEIGHT };
  GfxRect field = { 52, 172, 340, WM_DIALOG_BUTTON_HEIGHT };
  GfxRect accept = { 266, 196, WM_DIALOG_BUTTON_WIDTH,
    WM_DIALOG_BUTTON_HEIGHT };
  GfxRect cancel = { 332, 196, WM_DIALOG_BUTTON_WIDTH,
    WM_DIALOG_BUTTON_HEIGHT };
  GfxRect hidden = { 0, 0, 0, 0 };

  winAddLabel(widgets, 0, 0, 0, places, WIN_ALIGN_LEFT, "Places");
  winAddLabel(widgets, WM_DIALOG_LOOK_IN, 0, 0, lookIn, WIN_ALIGN_LEFT,
    "Look in:");
  winAddList(widgets, WM_DIALOG_PLACES, 0, 0, placesList, WIN_NO_ITEM, "", 0);
  winAddList(widgets, WM_DIALOG_ENTRIES, 0, 0, entries, WIN_NO_ITEM, "", 0);
  winAddLabel(widgets, 0, 0, 0, name, WIN_ALIGN_LEFT, "Name:");
  winAddTextField(widgets, WIN_DIALOG_FIELD, 0, 0, field, UINT8_MAX, "");
  winAddButton(widgets, WM_DIALOG_BUTTONS + WIN_RESULT_OK, WIN_WIDGET_DEFAULT,
    0, accept, (open->kind == WIN_DIALOG_SAVE_FILE) ? "Save" : "Open");
  winAddButton(widgets, WM_DIALOG_BUTTONS + WIN_RESULT_CANCEL,
    WIN_WIDGET_CANCEL, 0, cancel, "Cancel");
  winAddLabel(widgets, WIN_DIALOG_PATH, WIN_WIDGET_HIDDEN, 0, hidden,
    WIN_ALIGN_LEFT, "");
  frame->w = WM_FILE_WIDTH + (2 * WM_BORDER);
  frame->h = WM_FILE_HEIGHT + WM_FRAME_TOP + WM_BORDER;
}

/// @fn static void wmIcon(WmMakeJob *job, uint8_t icon)
///
/// @brief Make the canvas records of a message's icon, in flat shapes:  a
/// circle with "i", a triangle with "!", a circle with "x" or a circle with
/// "?".
///
/// @param job The job, whose icon and iconSize are set.
/// @param icon The WinDialogIcon.
///
/// @return This function returns no value.
static void wmIcon(WmMakeJob *job, uint8_t icon) {
  WmState *state = job->state;
  static const GfxColor faces[] = {
    WM_ACCENT, WM_ICON_WARNING, WM_ICON_ERROR, WM_ACCENT,
  };
  static const char marks[] = "i!x?";
  WinCanvas canvas;
  int16_t half = WM_DIALOG_ICON_SIZE / 2;

  winCanvasBegin(&canvas, job->icon, sizeof(job->icon));
  winCanvasColor(&canvas, faces[icon - 1]);

  if (icon == WIN_ICON_WARNING) {
    int16_t triangle[] = { half, 0, WM_DIALOG_ICON_SIZE - 1,
      WM_DIALOG_ICON_SIZE - 1, 0, WM_DIALOG_ICON_SIZE - 1 };
    uint8_t params[sizeof(triangle) + 1];

    memcpy(params, triangle, sizeof(triangle));
    params[sizeof(triangle)] = 1;
    winCanvasRecord_(&canvas, GFX_CMD_DRAW_TRIANGLE, params, sizeof(params));
    winCanvasColor(&canvas, WM_TEXT);
  } else {
    int16_t circle[] = { half, half, half - 1 };
    uint8_t params[sizeof(circle) + 1];

    memcpy(params, circle, sizeof(circle));
    params[sizeof(circle)] = 1;
    winCanvasRecord_(&canvas, GFX_CMD_DRAW_CIRCLE, params, sizeof(params));
    winCanvasColor(&canvas, WM_ON_ACCENT);
  }

  GfxPoint at = {
    (int16_t) (half - (state->display.builtInFont.glyphW / 2)),
    (int16_t) (half - (state->display.builtInFont.glyphH / 2)),
  };
  char mark[2] = { marks[icon - 1], '\0' };

  winCanvasText(&canvas, at, mark);

  job->iconSize = canvas.size;
}

/// @fn void* wmMake(void *args)
///
/// @brief Make a stock dialog's template, the size of its frame, the widget
/// that has the focus first, and its icon's records.
///
/// @param args A pointer to the WmMakeJob.
///
/// @return Always returns the provided pointer.
void* wmMake(void *args) {
  WmMakeJob *job = (WmMakeJob*) args;
  const WmDialogOpen *open = job->open;
  WinTemplate widgets;
  char *text = (char*) malloc(UINT8_MAX + 1);

  job->size = 0;
  job->iconSize = 0;

  if (text == NULL) {
    return args;
  }

  winTemplateBegin(&widgets, job->buffer, WM_DIALOG_TEMPLATE);

  if (open->kind == WIN_DIALOG_MESSAGE) {
    wmMessageTemplate(job->state, open, &widgets, &job->frame);
    job->focus = (uint8_t) (WM_DIALOG_BUTTONS
      + _wmButtonResults[open->buttons][0]);

    if (open->icon != WIN_ICON_NONE) {
      wmIcon(job, open->icon);
    }
  } else if (open->kind == WIN_DIALOG_INPUT) {
    wmInputTemplate(job->state, open, &widgets, &job->frame, text);
    job->focus = WIN_DIALOG_FIELD;
  } else {
    wmFileTemplate(job->state, open, &widgets, &job->frame);
    job->focus = (open->kind == WIN_DIALOG_OPEN_FILE) ? WM_DIALOG_ENTRIES
      : WIN_DIALOG_FIELD;
  }

  job->size = widgets.size;
  free(text);

  return args;
}
