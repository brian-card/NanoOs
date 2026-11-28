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


/// @file WmFiles.h
///
/// @brief What the Files and Listing overlays both use to work on a stock
/// file dialog:  its widgets, its requests to its client, and its places.

#ifndef WM_FILES_H
#define WM_FILES_H

#include "Wm.h"

/// @def WM_PATH_SIZE
///
/// @brief The bytes of a buffer for a path, its terminator included.
#define WM_PATH_SIZE (UINT8_MAX + 1)

/// @def WM_LISTING_MAX
///
/// @brief The most bytes of a listing's entries a file dialog keeps to sort.
#define WM_LISTING_MAX 2048

/// @def WM_MORE
///
/// @brief The last item of a list too long to show whole.
#define WM_MORE "..."

/// @fn static void wmSet(WmDialogJob *job, uint8_t widget,
///   uint8_t property, uint16_t value, const void *data, uint16_t size)
///
/// @brief Set a property of one of a file dialog's widgets as its client
/// would, through the Property overlay.
///
/// @param job The job.
/// @param widget The widget.
/// @param property The WinProperty.
/// @param value The value, for a property that takes one.
/// @param data The data, for a property that takes it.
/// @param size The bytes at data.
///
/// @return This function returns no value.
WM_SHARED void wmSet(WmDialogJob *job, uint8_t widget, uint8_t property,
  uint16_t value, const void *data, uint16_t size
) {
  WinSetWidgetArgs args;
  memset(&args, 0, sizeof(args));
  args.window = job->window->id;
  args.widget = widget;
  args.property = property;
  args.value = value;
  args.size = size;
  args.data = (const uint8_t*) data;

  WmTemplateJob set;
  memset(&set, 0, sizeof(set));
  set.state = job->state;
  set.window = job->window;
  set.data = &args;
  set.pid = job->window->owner;
  set.type = WIN_MSG_SET_WIDGET;
  set.waiting = true;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Property", "wmPropertyRequest",
    &set);
}

/// @fn static void wmSetText(WmDialogJob *job, uint8_t widget,
///   const char *text)
///
/// @brief Set the text of one of a file dialog's widgets.
///
/// @param job The job.
/// @param widget The widget.
/// @param text The text.
///
/// @return This function returns no value.
WM_SHARED void wmSetText(WmDialogJob *job, uint8_t widget, const char *text) {
  wmSet(job, widget, WIN_PROP_TEXT, 0, text, (uint16_t) strlen(text));
}

/// @fn static WmWidget* wmWidget(WmDialogJob *job, uint8_t id)
///
/// @brief Find one of a file dialog's widgets.
///
/// @param job The job.
/// @param id The widget's identifier.
///
/// @return Returns the widget, or NULL if it has gone.
WM_SHARED WmWidget* wmWidget(WmDialogJob *job, uint8_t id) {
  return wmWidgetById((WmWidgets*) job->window->widgets, id);
}

/// @fn static void wmTextOf(WmDialogJob *job, uint8_t id, char *text)
///
/// @brief Copy the text of one of a file dialog's widgets.
///
/// @param job The job.
/// @param id The widget's identifier.
/// @param text Where the text goes, NUL-ended:  WM_PATH_SIZE bytes.
///
/// @return This function returns no value.
WM_SHARED void wmTextOf(WmDialogJob *job, uint8_t id, char *text) {
  WmWidget *widget = wmWidget(job, id);
  uint8_t length = 0;

  if ((widget != NULL) && (widget->text != NULL)) {
    length = widget->textLength;
    memcpy(text, widget->text, length);
  }

  text[length] = '\0';
}

/// @fn static void wmRequest(WmDialogJob *job, const char *path,
///   uint8_t flags)
///
/// @brief Ask a file dialog's client for a listing:  the path goes in the
/// dialog's hidden path label, and the client is sent
/// WIN_EVENT_DIALOG_LIST_REQUEST.
///
/// @param job The job.
/// @param path The path.
/// @param flags The WIN_LIST_* bits.
///
/// @return This function returns no value.
WM_SHARED void wmRequest(WmDialogJob *job, const char *path, uint8_t flags) {
  WmDialog *dialog = job->dialog;

  if (((flags & WIN_LIST_PLACES) != 0)
    && (dialog->kind == WIN_DIALOG_SAVE_FILE)
  ) {
    flags |= WIN_LIST_CREATE;
  }

  wmSetText(job, WIN_DIALOG_PATH, path);
  dialog->pending = flags;
  dialog->listingSize = 0;
  dialog->truncated = false;

  WinEvent *event = wmSend(job->state, job->window->owner,
    WIN_EVENT_DIALOG_LIST_REQUEST, job->window->id);
  if (event != NULL) {
    event->value = flags;
  }
}

/// @fn static void wmFinish(WmDialogJob *job, uint8_t result)
///
/// @brief End a file dialog:  its client is sent WIN_EVENT_DIALOG_RESULT,
/// and for WIN_RESULT_OK reads the path chosen from the path label.
///
/// @param job The job.
/// @param result The WIN_RESULT_*.
///
/// @return This function returns no value.
WM_SHARED void wmFinish(WmDialogJob *job, uint8_t result) {
  job->dialog->ended = true;
  WinEvent *event = wmSend(job->state, job->window->owner,
    WIN_EVENT_DIALOG_RESULT, job->window->id);
  if (event != NULL) {
    event->value = result;
  }
}

/// @fn static const char* wmCutLeft(const char *path, size_t columns)
///
/// @brief Find how much of a path's end fits in some columns after "...",
/// starting at a '/' if one is in it.
///
/// @param path The path.
/// @param columns The columns, at least 4.
///
/// @return Returns the path itself if it fits, or the part of it to show
/// after "...".
WM_SHARED const char* wmCutLeft(const char *path, size_t columns) {
  size_t length = strlen(path);
  if (length <= columns) {
    return path;
  }

  const char *tail = &path[length - (columns - 3)];
  const char *slash = strchr(tail, '/');

  if ((slash != NULL) && (slash[1] != '\0')) {
    tail = slash;
  }

  return tail;
}

/// @fn static const char* wmPlace(const WmDialog *dialog, uint16_t index)
///
/// @brief Find one of a file dialog's places.
///
/// @param dialog The WmDialog.
/// @param index The place's index.
///
/// @return Returns the place's kind followed by its path, or NULL if there
/// is no such place.
WM_SHARED const char* wmPlace(const WmDialog *dialog, uint16_t index) {
  uint16_t at = 0;

  while (at < dialog->placesSize) {
    if (index == 0) {
      return &dialog->places[at];
    }

    at = (uint16_t) (at + strlen(&dialog->places[at + 1]) + 2);
    index--;
  }

  return NULL;
}

/// @fn static void wmNavigate(WmDialogJob *job, const char *path)
///
/// @brief Ask for the listing of a directory to show in a file dialog.
///
/// @param job The job.
/// @param path The directory, which gets a '/' after it if it has none.
///
/// @return This function returns no value.
WM_SHARED void wmNavigate(WmDialogJob *job, const char *path) {
  char *directory = (char*) malloc(WM_PATH_SIZE + 1);
  if (directory == NULL) {
    return;
  }

  size_t length = strlen(path);
  if (length > (WM_PATH_SIZE - 2)) {
    length = WM_PATH_SIZE - 2;
  }

  memcpy(directory, path, length);

  if ((length == 0) || (directory[length - 1] != '/')) {
    directory[length++] = '/';
  }

  directory[length] = '\0';
  wmRequest(job, directory, 0);
  free(directory);
}

#endif // WM_FILES_H
