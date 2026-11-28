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


/// @file Dialogs.c
///
/// @brief The stock dialogs:  opening a message, input or file dialog from
/// a template the Make overlay makes, carrying out what its buttons ask, and
/// taking a file dialog's listings, entered once per dialog request or
/// event, never per frame.  The Files and Listing overlays do the file
/// dialogs' work.

#include "Wm.h"

/// @fn static int wmOpen(WmState *state, const WmDialogOpen *open)
///
/// @brief Open a stock dialog, modal and owned by open->owner, for a client:
/// make its template and its window, and start it.
///
/// @param state The window manager's state.
/// @param open The dialog.
///
/// @return Returns the dialog's window, or the negated WinError for why it
/// couldn't be opened.
static int wmOpen(WmState *state, const WmDialogOpen *open) {
  WmMakeJob *make = (WmMakeJob*) calloc(1, sizeof(WmMakeJob));
  uint8_t *buffer = (uint8_t*) malloc(WM_DIALOG_TEMPLATE);
  WmDialog *dialog = (WmDialog*) calloc(1, sizeof(WmDialog));

  if ((make == NULL) || (buffer == NULL) || (dialog == NULL)) {
    free(make);
    free(buffer);
    free(dialog);
    return -WIN_ERR_NO_MEMORY;
  }

  make->state = state;
  make->open = open;
  make->buffer = buffer;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Make", "wmMake", make);

  WinCreateWindowArgs create;
  memset(&create, 0, sizeof(create));
  create.status = WIN_ERR_NO_MEMORY;
  create.layer = WIN_LAYER_NORMAL;
  create.flags = WIN_FLAG_DECORATED | WIN_FLAG_MODAL;
  create.owner = open->owner;
  create.titleLength = open->parts[0].length;
  create.templateSize = make->size;
  create.frame = make->frame;
  create.title = open->parts[0].text;
  create.widgets = buffer;

  if (make->size > 0) {
    WmWindowJob windowJob = { state, &create, open->pid, 0 };
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window", "wmWindowJob",
      &windowJob);
  }

  free(buffer);

  WmWindow *window = wmFind(state, create.window);
  if ((create.status != WIN_OK) || (window == NULL)) {
    free(make);
    free(dialog);
    return -create.status;
  }

  WmWidgets *widgets = (WmWidgets*) window->widgets;
  WmWidget *focus = wmWidgetById(widgets, make->focus);

  if (focus != NULL) {
    widgets->focus = (uint8_t) (focus - widgets->widget);
    focus->selected = (open->kind == WIN_DIALOG_INPUT)
      && (focus->textLength > 0);
  }

  window->dialog = dialog;
  dialog->kind = open->kind;
  dialog->parent = open->parent;
  dialog->maximum = open->maximum;
  dialog->pending = WM_NOT_PENDING;

  if (make->iconSize > 0) {
    WinBufferArgs args = { WIN_OK, window->id, WM_DIALOG_ICON,
      make->iconSize, make->icon };
    WmTemplateJob canvas;

    memset(&canvas, 0, sizeof(canvas));
    canvas.state = state;
    canvas.window = window;
    canvas.data = &args;
    canvas.pid = window->owner;
    canvas.type = WIN_MSG_SET_CANVAS;
    canvas.waiting = true;
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Canvas", "wmCanvasSet",
      &canvas);
  }

  free(make);

  if (open->kind >= WIN_DIALOG_OPEN_FILE) {
    char *initial = (char*) malloc(open->parts[2].length + 1);

    if (open->parts[3].length > 0) {
      dialog->suffix = (char*) malloc(open->parts[3].length + 1);
      if (dialog->suffix != NULL) {
        memcpy(dialog->suffix, open->parts[3].text, open->parts[3].length);
        dialog->suffix[open->parts[3].length] = '\0';
      }
    }

    if (initial != NULL) {
      memcpy(initial, open->parts[2].text, open->parts[2].length);
      initial[open->parts[2].length] = '\0';
      WmDialogJob job;
      memset(&job, 0, sizeof(job));
      job.state = state;
      job.window = window;
      job.dialog = dialog;
      job.initial = initial;
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Files", "wmFilesStart",
        &job);
      free(initial);
    }
  }

  return create.window;
}

/// @fn static void wmAsk(WmState *state, WmDialogJob *job)
///
/// @brief Open the message a file dialog asked for, owned by it, whose
/// answer goes back to it rather than to its client.
///
/// @param state The window manager's state.
/// @param job The job the Files overlay filled in.
///
/// @return This function returns no value.
static void wmAsk(WmState *state, WmDialogJob *job) {
  if (job->ask == NULL) {
    return;
  }

  WmWindow *window = job->window;
  bool replace = (job->dialog->asking == WM_ASK_REPLACE);
  WmDialogOpen open;

  memset(&open, 0, sizeof(open));
  open.kind = WIN_DIALOG_MESSAGE;
  open.icon = replace ? WIN_ICON_QUESTION : WIN_ICON_WARNING;
  open.buttons = replace ? WIN_BUTTONS_YES_NO : WIN_BUTTONS_OK;
  open.owner = window->id;
  open.parent = window->id;
  open.pid = window->owner;
  open.parts[0].text = window->title;
  open.parts[0].length = (window->title != NULL)
    ? (uint8_t) strlen(window->title) : 0;
  open.parts[1].text = job->ask;
  open.parts[1].length = (uint8_t) strlen(job->ask);

  if (wmOpen(state, &open) < 0) {
    job->dialog->asking = WM_ASK_NONE;
  }

  free(job->ask);
  job->ask = NULL;
}

/// @fn static void wmOpenRequest(WmState *state, ProcessId pid,
///   WinOpenDialogArgs *args)
///
/// @brief Carry out a client's WIN_MSG_OPEN_DIALOG.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param args The request's arguments.
///
/// @return This function returns no value.
static void wmOpenRequest(WmState *state, ProcessId pid,
  WinOpenDialogArgs *args
) {
  WmDialogOpen open;
  memset(&open, 0, sizeof(open));
  open.kind = args->kind;
  open.icon = args->icon;
  open.buttons = args->buttons;
  open.maximum = args->maximum;
  open.owner = args->owner;
  open.pid = pid;

  uint16_t at = 0;

  for (uint8_t ii = 0; ii < WM_DIALOG_PARTS; ii++) {
    if ((args->fields == NULL) || (at >= args->size)
      || ((at + 1 + args->fields[at]) > args->size)
    ) {
      args->status = WIN_ERR_INVALID;
      return;
    }

    open.parts[ii].length = args->fields[at];
    open.parts[ii].text = (const char*) &args->fields[at + 1];
    at = (uint16_t) (at + 1 + args->fields[at]);
  }

  if ((open.kind > WIN_DIALOG_SAVE_FILE) || (open.icon > WIN_ICON_QUESTION)
    || (open.buttons > WIN_BUTTONS_YES_NO_CANCEL)
    || ((open.kind != WIN_DIALOG_MESSAGE) && (open.maximum == 0))
  ) {
    args->status = WIN_ERR_INVALID;
    return;
  }

  int dialog = wmOpen(state, &open);
  if (dialog < 0) {
    args->status = (uint8_t) (-dialog);
    return;
  }

  args->dialog = (uint8_t) dialog;
  args->status = WIN_OK;
}

/// @fn static void wmFiles(WmState *state, WmDialogJob *job,
///   const char *overlay, const char *function)
///
/// @brief Have the Files or Listing overlay do a file dialog's work, then
/// open the message it asks for.  Having this here keeps one copy of the
/// job's setup.
///
/// @param state The window manager's state.
/// @param job The job, whose window and dialog are set, and whose event or
///   listing is if function takes one.
/// @param overlay "Files" or "Listing".
/// @param function The overlay's function.
///
/// @return This function returns no value.
static void wmFiles(WmState *state, WmDialogJob *job, const char *overlay,
  const char *function
) {
  job->state = state;
  job->ask = NULL;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, function, job);
  wmAsk(state, job);
}

/// @fn static void wmListingRequest(WmState *state, ProcessId pid,
///   WinDialogListingArgs *args)
///
/// @brief Take a part of the listing a file dialog asked its client for.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param args The request's arguments.
///
/// @return This function returns no value.
static void wmListingRequest(WmState *state, ProcessId pid,
  WinDialogListingArgs *args
) {
  WmWindow *window = wmFind(state, args->dialog);
  WmDialog *dialog = (window != NULL) ? (WmDialog*) window->dialog : NULL;

  if (dialog == NULL) {
    args->status = WIN_ERR_NOT_FOUND;
    return;
  } else if (window->owner != pid) {
    args->status = WIN_ERR_PERMISSION;
    return;
  } else if ((dialog->kind < WIN_DIALOG_OPEN_FILE)
    || (dialog->pending == WM_NOT_PENDING)
  ) {
    args->status = WIN_ERR_INVALID;
    return;
  }

  WmDialogJob job;
  memset(&job, 0, sizeof(job));
  job.window = window;
  job.dialog = dialog;
  job.listing = args;
  wmFiles(state, &job, "Listing", "wmListingTake");

  if (job.checked) {
    wmFiles(state, &job, "Files", "wmFilesChecked");
  } else if (job.placesShown) {
    wmFiles(state, &job, "Files", "wmFilesPlaces");
  }

  args->status = WIN_OK;
}

/// @fn void* wmDialogsMessage(void *args)
///
/// @brief Handle the WIN_MSG_OPEN_DIALOG or WIN_MSG_DIALOG_LISTING in the
/// state, and finish its message.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmDialogsMessage(void *args) {
  WmState *state = (WmState*) args;
  ProcessMessage *message = state->message;
  void *data = processMessageData(message);
  bool waiting = processMessageWaiting(message);

  if ((data != NULL) && waiting) {
    ProcessId pid = processMessageFrom(message)->processId;

    if (((uint8_t) processMessageType(message)) == WIN_MSG_OPEN_DIALOG) {
      wmOpenRequest(state, pid, (WinOpenDialogArgs*) data);
    } else {
      wmListingRequest(state, pid, (WinDialogListingArgs*) data);
    }
  } else if (data != NULL) {
    MESSAGE_ARGS_DESTROY(data);
  }

  processMessageSetDone(message);

  if (waiting == false) {
    processMessageRelease(message);
  }

  return args;
}

/// @fn static void wmEnd(WmState *state, WmWindow *window,
///   WmDialog *dialog, uint8_t result)
///
/// @brief End a message or input dialog with a result:  its client is sent
/// WIN_EVENT_DIALOG_RESULT, or, for a message of the window server's own,
/// the file dialog it answers is told and the message taken away.
///
/// @param state The window manager's state.
/// @param window The dialog.
/// @param dialog Its WmDialog.
/// @param result The WIN_RESULT_*.
///
/// @return This function returns no value.
static void wmEnd(WmState *state, WmWindow *window, WmDialog *dialog,
  uint8_t result
) {
  dialog->ended = true;

  if (dialog->parent == 0) {
    WinEvent *event = wmSend(state, window->owner, WIN_EVENT_DIALOG_RESULT,
      window->id);
    if (event != NULL) {
      event->value = result;
    }

    return;
  }

  uint8_t parent = dialog->parent;
  WmWindowJob windowJob = { state, NULL, 0, window->id };

  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window", "wmWindowJob",
    &windowJob);

  WmWindow *file = wmFind(state, parent);
  if ((file != NULL) && (file->dialog != NULL)) {
    WmDialogJob job;
    memset(&job, 0, sizeof(job));
    job.window = file;
    job.dialog = (WmDialog*) file->dialog;
    job.result = result;
    wmFiles(state, &job, "Files", "wmFilesAnswer");
  }
}

/// @fn void* wmDialogsEvents(void *args)
///
/// @brief Carry out the events about stock dialogs the Client overlay kept:
/// a button or a request to close ends a message or input dialog, and a
/// file dialog's events are the Files overlay's.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmDialogsEvents(void *args) {
  WmState *state = (WmState*) args;

  for (uint8_t ii = 0; ii < state->numDialogEvents; ii++) {
    const WinEvent *event = &state->dialogEvents[ii];
    WmWindow *window = wmFind(state, event->window);
    WmDialog *dialog = (window != NULL) ? (WmDialog*) window->dialog : NULL;

    if ((dialog == NULL) || dialog->ended) {
      continue;
    } else if (dialog->kind >= WIN_DIALOG_OPEN_FILE) {
      WmDialogJob job;
      memset(&job, 0, sizeof(job));
      job.window = window;
      job.dialog = dialog;
      job.event = event;
      wmFiles(state, &job, "Files", "wmFilesEvent");
    } else if (event->type == WIN_EVENT_CLOSE_REQUESTED) {
      wmEnd(state, window, dialog, WIN_RESULT_CANCEL);
    } else if ((event->type == WIN_EVENT_COMMAND)
      && (event->widget >= WM_DIALOG_BUTTONS)
      && (event->widget <= (WM_DIALOG_BUTTONS + WIN_RESULT_NO))
    ) {
      wmEnd(state, window, dialog,
        (uint8_t) (event->widget - WM_DIALOG_BUTTONS));
    }
  }

  state->numDialogEvents = 0;

  return args;
}
