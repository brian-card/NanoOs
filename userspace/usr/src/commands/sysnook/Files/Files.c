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


/// @file Files.c
///
/// @brief The stock file dialogs' work on their widgets' events:  starting
/// one, moving between directories, and choosing a file, entered from the
/// Dialogs overlay only.  The client reads every directory itself, with its
/// own capabilities, and the Listing overlay takes what it sends.

#include "WmFiles.h"

/// @fn static bool wmResolve(const char *directory, const char *name,
///   char *path)
///
/// @brief Make the absolute path a name in a file dialog names:  a name
/// starting with '/' is absolute, and any other is taken from the
/// directory shown.  "." and ".." components are taken out.
///
/// @param directory The directory shown, ending in '/', or NULL.
/// @param name The name.
/// @param path Where the path goes:  WM_PATH_SIZE bytes.
///
/// @return Returns true if the path fits, false otherwise.
static bool wmResolve(const char *directory, const char *name, char *path) {
  char *joined = (char*) malloc((2 * WM_PATH_SIZE) + 1);
  if (joined == NULL) {
    return false;
  }

  snprintf(joined, (2 * WM_PATH_SIZE) + 1, "%s%s",
    ((name[0] == '/') || (directory == NULL)) ? "" : directory, name);
  size_t length = 0;
  bool fits = true;
  char *part = joined;

  while ((*part != '\0') && fits) {
    char *end = strchr(part, '/');
    size_t partLength = (end != NULL) ? (size_t) (end - part) : strlen(part);

    if ((partLength == 2) && (part[0] == '.') && (part[1] == '.')) {
      while ((length > 0) && (path[length - 1] == '/')) {
        length--;
      }
      while ((length > 0) && (path[length - 1] != '/')) {
        length--;
      }
    } else if ((partLength != 0) && (!((partLength == 1) && (*part == '.')))) {
      if ((length == 0) || (path[length - 1] != '/')) {
        path[length++] = '/';
      }

      if ((length + partLength + 1) >= WM_PATH_SIZE) {
        fits = false;
      } else {
        memcpy(&path[length], part, partLength);
        length += partLength;
      }
    }

    part += partLength;

    if (*part == '/') {
      part++;
    }
  }

  if (length == 0) {
    path[length++] = '/';
  }

  size_t nameLength = strlen(name);

  if (fits && (nameLength > 0) && (name[nameLength - 1] == '/')
    && (path[length - 1] != '/')
  ) {
    path[length++] = '/';
  }

  path[length] = '\0';
  free(joined);

  return fits;
}

/// @fn static void wmAccept(WmDialogJob *job, const char *name)
///
/// @brief Carry out Open or Save on a name:  a directory's name, or a path
/// ending in '/', is shown, and anything else is checked with the client.
///
/// @param job The job.
/// @param name The name.
///
/// @return This function returns no value.
static void wmAccept(WmDialogJob *job, const char *name) {
  WmDialog *dialog = job->dialog;
  char *path = (char*) malloc(WM_PATH_SIZE);

  if ((name[0] == '\0') || (path == NULL)) {
    free(path);
    return;
  }

  bool fits = wmResolve(dialog->directory, name, path);
  size_t length = strlen(path);

  if (fits && (path[length - 1] == '/')) {
    wmNavigate(job, path);
  } else if ((!fits) || (length > dialog->maximum)) {
    wmSetText(job, WM_DIALOG_LOOK_IN, "Name too long");
  } else {
    wmRequest(job, path, WIN_LIST_CHECK);
  }

  free(path);
}

/// @fn static void wmChecked(WmDialogJob *job, const char *path,
///   uint8_t errorNumber)
///
/// @brief Carry out what a check of the path chosen found:  a directory is
/// shown, a file that exists is opened or, after asking, replaced, and a
/// file that doesn't is saved to or said to be missing.
///
/// @param job The job, whose errorNumber, 0 or ENOENT, and kind say what was
///   found.
/// @param path The path checked.
///
/// @return This function returns no value.
static void wmChecked(WmDialogJob *job, const char *path) {
  WmDialog *dialog = job->dialog;
  uint8_t errorNumber = job->errorNumber;
  bool open = (dialog->kind == WIN_DIALOG_OPEN_FILE);
  const char *slash = strrchr(path, '/');
  const char *name = (slash != NULL) ? (slash + 1) : path;

  if (errorNumber == ENOENT) {
    if (!open) {
      wmFinish(job, WIN_RESULT_OK);
      return;
    }

    dialog->asking = WM_ASK_MISSING;
    job->ask = (char*) malloc(strlen(name) + sizeof(": no such file"));
    if (job->ask != NULL) {
      sprintf(job->ask, "%s: no such file", name);
    }
  } else if (job->kind == WIN_ENTRY_DIRECTORY) {
    wmNavigate(job, path);
  } else if (open) {
    wmFinish(job, WIN_RESULT_OK);
  } else {
    dialog->asking = WM_ASK_REPLACE;
    job->ask = (char*) malloc(strlen(name) + sizeof("Replace ?"));
    if (job->ask != NULL) {
      sprintf(job->ask, "Replace %s?", name);
    }
  }
}

/// @fn void* wmFilesChecked(void *args)
///
/// @brief Carry out what the check of the path chosen found, for the Dialogs
/// overlay, once the Listing overlay has taken the check's answer.
///
/// @param args A pointer to the WmDialogJob, whose errorNumber and kind are
///   set.
///
/// @return Always returns the provided pointer.
void* wmFilesChecked(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;
  char *path = (char*) malloc(WM_PATH_SIZE);

  if (path != NULL) {
    wmTextOf(job, WIN_DIALOG_PATH, path);
    wmChecked(job, path);
    free(path);
  }

  return args;
}

/// @fn static void wmShowPlaces(WmDialogJob *job, const char *directory)
///
/// @brief Show a file dialog's places, the home directory as "~/" and each
/// path cut from the left to fit, with the longest one holding the directory
/// shown highlighted.
///
/// @param job The job.
/// @param directory The directory shown, ending in '/'.
///
/// @return This function returns no value.
static void wmShowPlaces(WmDialogJob *job, const char *directory) {
  WmDialog *dialog = job->dialog;
  WmWidget *list = wmWidget(job, WM_DIALOG_PLACES);
  char *items = (char*) malloc(WIN_LIST_ITEMS_MAX);

  if ((list == NULL) || (items == NULL)) {
    free(items);
    return;
  }

  size_t columns = (list->rect.w - WM_SCROLL_BAR_WIDTH - 4)
    / job->state->display.builtInFont.glyphW;
  uint16_t size = 0;
  uint16_t highlight = WIN_NO_ITEM;
  size_t longest = 0;
  const char *place = NULL;

  for (uint16_t ii = 0; (place = wmPlace(dialog, ii)) != NULL; ii++) {
    const char *path = &place[1];
    size_t length = strlen(path);
    const char *shown = wmCutLeft(path, columns);
    char item[WM_PATH_SIZE + 3];

    if (place[0] == WIN_ENTRY_HOME) {
      strcpy(item, "~/");
    } else {
      snprintf(item, sizeof(item), "%s%s", (shown != path) ? "..." : "",
        shown);
    }

    size_t itemSize = strlen(item) + 1;

    if ((size + itemSize) > WIN_LIST_ITEMS_MAX) {
      break;
    }

    memcpy(&items[size], item, itemSize);
    size = (uint16_t) (size + itemSize);

    if ((length > longest) && (strncmp(directory, path, length) == 0)) {
      longest = length;
      highlight = ii;
    }
  }

  wmSet(job, WM_DIALOG_PLACES, WIN_PROP_ITEMS, 0, items, size);
  wmSet(job, WM_DIALOG_PLACES, WIN_PROP_VALUE, highlight, NULL, 0);
  free(items);
}

/// @fn void* wmFilesPlaces(void *args)
///
/// @brief Show a file dialog's places with the one holding its directory
/// highlighted, for the Dialogs overlay, once the Listing overlay has shown
/// the directory.
///
/// @param args A pointer to the WmDialogJob.
///
/// @return Always returns the provided pointer.
void* wmFilesPlaces(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;

  if (job->dialog->directory != NULL) {
    wmShowPlaces(job, job->dialog->directory);
  }

  return args;
}

/// @fn void* wmFilesStart(void *args)
///
/// @brief Start a file dialog just made:  the initial path's name goes in
/// the name field, selected in a save dialog, and the places and the
/// initial directory are asked for.
///
/// @param args A pointer to the WmDialogJob, whose initial is set.
///
/// @return Always returns the provided pointer.
void* wmFilesStart(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;
  const char *initial = job->initial;
  const char *slash = strrchr(initial, '/');
  const char *name = initial;
  char *directory = (char*) malloc(WM_PATH_SIZE);

  if (directory == NULL) {
    return args;
  }

  directory[0] = '\0';

  if ((initial[0] == '/') && (slash != NULL)) {
    size_t length = ((size_t) (slash - initial)) + 1;
    memcpy(directory, initial, length);
    directory[length] = '\0';
    name = slash + 1;
  }

  if (*name != '\0') {
    wmSetText(job, WIN_DIALOG_FIELD, name);

    WmWidget *field = wmWidget(job, WIN_DIALOG_FIELD);
    if ((field != NULL) && (job->dialog->kind == WIN_DIALOG_SAVE_FILE)) {
      field->selected = true;
    }
  }

  wmRequest(job, directory, WIN_LIST_PLACES);
  free(directory);

  return args;
}

/// @fn static const char* wmListItem(WmDialogJob *job, uint8_t list,
///   uint16_t index)
///
/// @brief Find an item of one of a file dialog's lists.
///
/// @param job The job.
/// @param list The list's identifier.
/// @param index The item's index.
///
/// @return Returns the item, NUL-ended, or NULL if there is none.
static const char* wmListItem(WmDialogJob *job, uint8_t list,
  uint16_t index
) {
  WmWidget *widget = wmWidget(job, list);
  uint16_t length = 0;

  if ((widget == NULL) || (index >= widget->numItems)) {
    return NULL;
  }

  return wmItem(widget, index, &length);
}

/// @fn static void wmOpenEntry(WmDialogJob *job, const char *item)
///
/// @brief Carry out a double click or Enter on an entry:  a directory is
/// shown, and a file is chosen.
///
/// @param job The job.
/// @param item The entry's item.
///
/// @return This function returns no value.
static void wmOpenEntry(WmDialogJob *job, const char *item) {
  size_t length = strlen(item);

  if ((length == 0) || (strcmp(item, WM_MORE) == 0)) {
    return;
  } else if (item[length - 1] == '/') {
    wmAccept(job, item);
    return;
  }

  wmSetText(job, WIN_DIALOG_FIELD, item);
  wmAccept(job, item);
}

/// @fn void* wmFilesEvent(void *args)
///
/// @brief Carry out an event about a file dialog's widgets:  a place or an
/// entry chosen, a file entry selected, Open or Save, Cancel, or a request
/// to close.  Nothing but Cancel is carried out while a listing is awaited.
///
/// @param args A pointer to the WmDialogJob, whose event is set.
///
/// @return Always returns the provided pointer.
void* wmFilesEvent(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;
  const WinEvent *event = job->event;
  uint8_t widget = event->widget;
  bool command = (event->type == WIN_EVENT_COMMAND);
  bool changed = (event->type == WIN_EVENT_CHANGED);

  if ((event->type == WIN_EVENT_CLOSE_REQUESTED)
    || (command && (widget == (WM_DIALOG_BUTTONS + WIN_RESULT_CANCEL)))
  ) {
    wmFinish(job, WIN_RESULT_CANCEL);
    return args;
  } else if ((job->dialog->pending != WM_NOT_PENDING)
    || (job->dialog->asking != WM_ASK_NONE)
  ) {
    return args;
  }

  if (command && (widget == (WM_DIALOG_BUTTONS + WIN_RESULT_OK))) {
    char *name = (char*) malloc(WM_PATH_SIZE);
    if (name != NULL) {
      wmTextOf(job, WIN_DIALOG_FIELD, name);
      wmAccept(job, name);
      free(name);
    }
  } else if ((command || changed) && (widget == WM_DIALOG_PLACES)) {
    const char *place = wmPlace(job->dialog, event->value);
    if (place != NULL) {
      wmNavigate(job, &place[1]);
    }
  } else if (widget == WM_DIALOG_ENTRIES) {
    const char *item = wmListItem(job, WM_DIALOG_ENTRIES, event->value);
    size_t length = (item != NULL) ? strlen(item) : 0;

    if (command && (item != NULL)) {
      wmOpenEntry(job, item);
    } else if (changed && (length > 0) && (item[length - 1] != '/')
      && (strcmp(item, WM_MORE) != 0)
    ) {
      wmSetText(job, WIN_DIALOG_FIELD, item);
    }
  }

  return args;
}

/// @fn void* wmFilesAnswer(void *args)
///
/// @brief Carry out the answer to a file dialog's own message:  Yes to
/// replacing a file chooses it.
///
/// @param args A pointer to the WmDialogJob, whose result is set.
///
/// @return Always returns the provided pointer.
void* wmFilesAnswer(void *args) {
  WmDialogJob *job = (WmDialogJob*) args;
  WmDialog *dialog = job->dialog;
  uint8_t asking = dialog->asking;

  dialog->asking = WM_ASK_NONE;

  if ((asking == WM_ASK_REPLACE) && (job->result == WIN_RESULT_YES)) {
    wmFinish(job, WIN_RESULT_OK);
  }

  return args;
}
