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




/// @file Widget.c
///
/// @brief wintest's commands that build a template of widgets.

#include "Wintest.h"

/// @struct WintestKind
///
/// @brief A widget record command:  its name, its WinWidgetKind, and how
/// many of the kind's fields come before its text, each a word.
///
/// @param name The command.
/// @param kind The WinWidgetKind.
/// @param numFields The number of field words.
typedef struct WintestKind {
  const char *name;
  uint8_t     kind;
  uint8_t     numFields;
} WintestKind;

/// @var _kinds
///
/// @brief The widget record commands:  label ID X,Y,W,H ALIGN TEXT,
/// group ID X,Y,W,H CAPTION, button ID X,Y,W,H TEXT,
/// toggle ID X,Y,W,H VALUE TEXT, check ID X,Y,W,H VALUE TEXT,
/// radio ID X,Y,W,H GROUP VALUE TEXT and field ID X,Y,W,H MAX TEXT.
static const WintestKind _kinds[] = {
  { "label", WIN_WIDGET_LABEL, 1 },
  { "group", WIN_WIDGET_GROUP, 0 },
  { "button", WIN_WIDGET_BUTTON, 0 },
  { "toggle", WIN_WIDGET_TOGGLE, 1 },
  { "check", WIN_WIDGET_CHECK_BOX, 1 },
  { "radio", WIN_WIDGET_RADIO, 2 },
  { "field", WIN_WIDGET_TEXT_FIELD, 1 },
};

/// @fn static uint8_t field(const char *word)
///
/// @brief Read a field word:  left, center or right as a WIN_ALIGN_*
/// value, or a number.
///
/// @param word The word.
///
/// @return Returns the field's byte.
static uint8_t field(const char *word) {
  if (strcmp(word, "center") == 0) {
    return WIN_ALIGN_CENTER;
  } else if (strcmp(word, "right") == 0) {
    return WIN_ALIGN_RIGHT;
  }
  return (uint8_t) strtol(word, NULL, 0);
}

/// @fn static int list(Wintest *wintest, int ii)
///
/// @brief Add a list record to the template:  list ID X,Y,W,H SELECTION
/// ITEMS, then options.  SELECTION is an index or "none", and ITEMS is
/// separated by commas.
///
/// @param wintest The context.
/// @param ii The index of the command's word.
///
/// @return Returns the index of the word after the command, or -1 if it is
/// malformed.
static int list(Wintest *wintest, int ii) {
  char **argv = wintest->argv;
  GfxRect rect;
  if ((ii + 4 >= wintest->argc) || !parseRect(argv[ii + 2], &rect)) {
    return -1;
  }
  uint16_t selection = WIN_NO_ITEM;
  if (strcmp(argv[ii + 3], "none") != 0) {
    selection = (uint16_t) strtol(argv[ii + 3], NULL, 0);
  }
  uint8_t flags = 0;
  uint8_t anchor = 0;
  int next = options(wintest, ii + 5, &flags, &anchor);
  WinTemplate *widgets = pending(wintest);
  uint16_t size = 0;
  char *items = listItems(argv[ii + 4], &size);
  bool added = (widgets != NULL) && (items != NULL)
    && winAddList(widgets, (uint8_t) strtol(argv[ii + 1], NULL, 0), flags,
      anchor, rect, selection, items, size);
  free(items);
  if (!added) {
    printf("wintest: list %s: template full\n", argv[ii + 1]);
  }
  return next;
}

/// @fn static bool menuItem(WinMenus *menus, char *item)
///
/// @brief Add a menu item written ID=TEXT[:SHORTCUT][/FLAGS], or a "-"
/// separator, to the last menu.  FLAGS holds d for disabled and c for
/// checked.
///
/// @param menus The menus.
/// @param item The item, which is cut up.
///
/// @return Returns true if it was added, false otherwise.
static bool menuItem(WinMenus *menus, char *item) {
  if (strcmp(item, "-") == 0) {
    return winMenusAddItem(menus, 0, WIN_MENU_SEPARATOR, NULL, NULL);
  }
  char *text = strchr(item, '=');
  if (text == NULL) {
    return false;
  }
  *text++ = '\0';
  uint8_t flags = 0;
  char *bits = strchr(text, '/');
  if (bits != NULL) {
    *bits++ = '\0';
    if (strchr(bits, 'd') != NULL) {
      flags |= WIN_MENU_DISABLED;
    }
    if (strchr(bits, 'c') != NULL) {
      flags |= WIN_MENU_CHECKED;
    }
  }
  char *shortcut = strchr(text, ':');
  if (shortcut != NULL) {
    *shortcut++ = '\0';
  }
  return winMenusAddItem(menus, (uint8_t) strtol(item, NULL, 0), flags, text,
    shortcut);
}

/// @fn static int menu(Wintest *wintest, int ii)
///
/// @brief Add a menu bar record to the template:  menu ID X,Y,W,H MENUS,
/// then options.  MENUS is menus separated by '|', each a title, then its
/// items, separated by commas.
///
/// @param wintest The context.
/// @param ii The index of the command's word.
///
/// @return Returns the index of the word after the command, or -1 if it is
/// malformed.
static int menu(Wintest *wintest, int ii) {
  char **argv = wintest->argv;
  GfxRect rect;
  if ((ii + 3 >= wintest->argc) || !parseRect(argv[ii + 2], &rect)) {
    return -1;
  }
  uint8_t flags = 0;
  uint8_t anchor = 0;
  int next = options(wintest, ii + 4, &flags, &anchor);
  WinTemplate *widgets = pending(wintest);
  char *spec = spaced(argv[ii + 3]);
  uint8_t *buffer = (uint8_t*) malloc(WIN_MENU_BAR_MAX);
  WinMenus menus;
  winMenusBegin(&menus, buffer, WIN_MENU_BAR_MAX);
  bool added = (widgets != NULL) && (spec != NULL) && (buffer != NULL);
  for (char *title = spec; added && (title != NULL); ) {
    char *after = strchr(title, '|');
    if (after != NULL) {
      *after++ = '\0';
    }
    char *item = strchr(title, ',');
    if (item != NULL) {
      *item++ = '\0';
    }
    added = winMenusAdd(&menus, title);
    while (added && (item != NULL)) {
      char *following = strchr(item, ',');
      if (following != NULL) {
        *following++ = '\0';
      }
      added = menuItem(&menus, item);
      item = following;
    }
    title = after;
  }
  added = added && winAddMenuBar(widgets,
    (uint8_t) strtol(argv[ii + 1], NULL, 0), flags, rect, &menus);
  free(buffer);
  free(spec);
  if (!added) {
    printf("wintest: menu %s: template full\n", argv[ii + 1]);
  }
  return next;
}

/// @fn static int record(Wintest *wintest, int ii)
///
/// @brief Add a widget record to the template:  one of _kinds' commands,
/// then options.
///
/// @param wintest The context.
/// @param ii The index of the command's word.
///
/// @return Returns the index of the word after the command, or -1 if it
/// isn't one of these or is malformed.
static int record(Wintest *wintest, int ii) {
  char **argv = wintest->argv;
  const char *command = argv[ii];
  const WintestKind *kind = NULL;
  for (size_t jj = 0; jj < WINTEST_COUNT(_kinds); jj++) {
    if (strcmp(command, _kinds[jj].name) == 0) {
      kind = &_kinds[jj];
    }
  }
  GfxRect rect;
  if ((kind == NULL) || (ii + 3 + kind->numFields >= wintest->argc)
    || !parseRect(argv[ii + 2], &rect)
  ) {
    return -1;
  }
  uint8_t fields[2];
  for (uint8_t jj = 0; jj < kind->numFields; jj++) {
    fields[jj] = field(argv[ii + 3 + jj]);
  }
  int textAt = ii + 3 + kind->numFields;
  uint8_t flags = 0;
  uint8_t anchor = 0;
  int next = options(wintest, textAt + 1, &flags, &anchor);
  WinTemplate *widgets = pending(wintest);
  // A text of its own can't be an empty word, so "-" stands for none.
  char *text = spaced((strcmp(argv[textAt], "-") == 0) ? "" : argv[textAt]);
  bool added = (widgets != NULL) && (text != NULL)
    && winAddRecord_(widgets, kind->kind,
      (uint8_t) strtol(argv[ii + 1], NULL, 0), flags, anchor, rect, fields,
      kind->numFields, text);
  free(text);
  if (!added) {
    printf("wintest: %s %s: template full\n", command, argv[ii + 1]);
  }
  return next;
}

/// @fn void* wintestWidget(void *args)
///
/// @brief Carry out the widget record command at the context's index, one
/// of _kinds', list or menu, and move the index past it.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestWidget(void *args) {
  Wintest *wintest = (Wintest*) args;
  int next = -1;
  const char *command = wintest->argv[wintest->index];
  if (strcmp(command, "list") == 0) {
    next = list(wintest, wintest->index);
  } else if (strcmp(command, "menu") == 0) {
    next = menu(wintest, wintest->index);
  } else {
    next = record(wintest, wintest->index);
  }
  wintest->handled = (next >= 0);
  if (next < 0) {
    return args;
  }
  wintest->index = next;
  return args;
}
