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




/// @file Template.c
///
/// @brief A window's contents as the window server keeps them:  making its
/// text grid or adding a template's widgets to it, freeing them, and
/// checking the widget requests that the Property overlay carries out.

#include "Wm.h"

/// @def WM_UNSUPPORTED
///
/// @brief What _wmFields holds for a kind this window server doesn't have.
#define WM_UNSUPPORTED 0xFF

/// @var _wmFields
///
/// @brief The bytes of each widget kind's fields that come before its text,
/// or WM_UNSUPPORTED:  a label's alignment, a toggle button's or check box's
/// value, a radio button's group and value, and a list's selection and the
/// size of its items, and a menu bar's size of its menus, which take the
/// text's place.
static const uint8_t _wmFields[] = {
  1,                    // WIN_WIDGET_LABEL
  0,                    // WIN_WIDGET_BUTTON
  1,                    // WIN_WIDGET_TOGGLE
  1,                    // WIN_WIDGET_CHECK_BOX
  2,                    // WIN_WIDGET_RADIO
  1,                    // WIN_WIDGET_TEXT_FIELD
  2 * sizeof(uint16_t), // WIN_WIDGET_LIST
  0,                    // WIN_WIDGET_GROUP
  0,                    // WIN_WIDGET_CANVAS
  WM_UNSUPPORTED,       // WIN_WIDGET_TEXT_GRID
  sizeof(uint16_t),     // WIN_WIDGET_MENU_BAR
};
GFX_STATIC_ASSERT(sizeof(_wmFields) == WIN_WIDGET_COUNT,
  "_wmFields needs an entry for each WinWidgetKind");
GFX_STATIC_ASSERT((WIN_WIDGET_LIST == 6) && (WIN_WIDGET_MENU_BAR == 10),
  "_wmFields' entries are in WinWidgetKind's order");

/// @fn static uint16_t wmItemsSize(const uint8_t *record)
///
/// @brief Read the size of a list record's items or a menu bar record's
/// menus, the last of its fields.
///
/// @param record The list or menu bar record.
///
/// @return Returns the bytes of its items or menus.
static uint16_t wmItemsSize(const uint8_t *record) {
  const uint8_t *size = &record[WIN_RECORD_HEADER_SIZE
    + _wmFields[record[WIN_RECORD_KIND]] - sizeof(uint16_t)];
  return (uint16_t) (size[0] | (size[1] << 8));
}

/// @fn static int16_t wmCountMenus(const uint8_t *menus, uint16_t size)
///
/// @brief Have the Property overlay check a menu bar's menus and count
/// them.  Checking them there keeps this overlay inside its window.
///
/// @param menus The menus.
/// @param size The bytes at menus.
///
/// @return Returns the number of menus, or -1 if they are malformed.
static int16_t wmCountMenus(const uint8_t *menus, uint16_t size) {
  WmMenusCheck check = { menus, size, -1 };
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Property", "wmPropertyMenus",
    &check);
  return check.count;
}

/// @fn static uint16_t wmRecordSize(const uint8_t *record, uint16_t left,
///   uint8_t *status)
///
/// @brief Find how many bytes a template record takes, its header and its
/// kind's fields, checking that they are all there.
///
/// @param record The record.
/// @param left The bytes from the record to the template's end.
/// @param status Where to store WIN_ERR_INVALID for a record cut short or
///   WIN_ERR_NOT_SUPPORTED for a kind this window server doesn't have.
///
/// @return Returns the record's size, or 0 if it can't be used.
static uint16_t wmRecordSize(const uint8_t *record, uint16_t left,
  uint8_t *status
) {
  uint8_t kind = (left > WIN_RECORD_KIND) ? record[WIN_RECORD_KIND] : 0;
  if (kind >= WIN_WIDGET_COUNT) {
    *status = WIN_ERR_INVALID;
    return 0;
  } else if (_wmFields[kind] == WM_UNSUPPORTED) {
    *status = WIN_ERR_NOT_SUPPORTED;
    return 0;
  }
  bool list = (kind == WIN_WIDGET_LIST) || (kind == WIN_WIDGET_MENU_BAR);
  uint16_t size = WIN_RECORD_HEADER_SIZE + _wmFields[kind];
  if (kind == WIN_WIDGET_CANVAS) {
    if (left < size) {
      *status = WIN_ERR_INVALID;
      return 0;
    }
    return size;
  }
  if ((left < size) || (!list && (left == size))
    || (list && (wmItemsSize(record) > WIN_LIST_ITEMS_MAX))
  ) {
    *status = WIN_ERR_INVALID;
    return 0;
  }
  if (list) {
    size += wmItemsSize(record);
  } else {
    size += 1 + record[size];
  }
  if (size > left) {
    *status = WIN_ERR_INVALID;
    return 0;
  }
  return size;
}

/// @fn static const uint8_t* wmRecordText(const uint8_t *record)
///
/// @brief Find a template record's text:  its length byte, then its bytes.
///
/// @param record The record.
///
/// @return Returns a pointer to the text's length byte.
static const uint8_t* wmRecordText(const uint8_t *record) {
  return &record[WIN_RECORD_HEADER_SIZE + _wmFields[record[WIN_RECORD_KIND]]];
}

/// @fn static uint8_t wmMaxLength(const uint8_t *record)
///
/// @brief Find a text field record's most bytes, 255 if it gives 0.
///
/// @param record The record.
///
/// @return Returns the bytes.
static uint8_t wmMaxLength(const uint8_t *record) {
  uint8_t most = record[WIN_RECORD_HEADER_SIZE];
  return (most == 0) ? UINT8_MAX : most;
}

/// @fn static void wmFreeWidgetTexts(WmWidgets *widgets, uint8_t from)
///
/// @brief Free the texts of a window's widgets from one on.
///
/// @param widgets The window's widgets.
/// @param from The index of the first widget whose text to free.
///
/// @return This function returns no value.
static void wmFreeWidgetTexts(WmWidgets *widgets, uint8_t from) {
  for (uint8_t ii = from; ii < widgets->count; ii++) {
    free(widgets->widget[ii].text);
  }
}

/// @fn static uint8_t wmAddTemplate(WmState *state, WmWindow *window,
///   const uint8_t *records, uint16_t size)
///
/// @brief Add a template's widgets to a window, all of them or, if any can't
/// be, none:  the window and the owner's budget are then as they were.
///
/// @param state The window manager's state.
/// @param window The window, whose frame and owner are set.
/// @param records The template.
/// @param size The bytes in the template.
///
/// @return Returns WIN_OK, WIN_ERR_INVALID for a malformed template or a
/// repeated identifier, WIN_ERR_NOT_SUPPORTED for a kind this window server
/// doesn't have, or WIN_ERR_NO_MEMORY for more than WIN_MAX_WIDGETS widgets
/// in the window, a template past the owner's budget, or a heap that is
/// full.
static uint8_t wmAddTemplate(WmState *state, WmWindow *window,
  const uint8_t *records, uint16_t size
) {
  WmWidgets *old = (WmWidgets*) window->widgets;
  uint8_t oldCount = (old != NULL) ? old->count : 0;
  uint16_t oldBytes = (old != NULL) ? old->bytes : 0;
  uint8_t status = WIN_OK;
  uint16_t count = 0;
  uint32_t textBytes = 0;
  if ((records == NULL) || (size == 0)) {
    return WIN_ERR_INVALID;
  }
  for (uint16_t at = 0; at < size; count++) {
    uint16_t length = wmRecordSize(&records[at], size - at, &status);
    if (length == 0) {
      return status;
    }
    uint8_t id = records[at + WIN_RECORD_ID];
    bool repeated = (id != WIN_NONE) && (wmWidgetById(old, id) != NULL);
    for (uint16_t earlier = 0; !repeated && (earlier < at); ) {
      repeated = (id != WIN_NONE) && (records[earlier + WIN_RECORD_ID] == id);
      earlier += wmRecordSize(&records[earlier], size - earlier, &status);
    }
    if (repeated) {
      return WIN_ERR_INVALID;
    }
    uint8_t kind = records[at + WIN_RECORD_KIND];
    if (kind == WIN_WIDGET_TEXT_FIELD) {
      textBytes += wmMaxLength(&records[at]);
    } else if ((kind == WIN_WIDGET_LIST) || (kind == WIN_WIDGET_MENU_BAR)) {
      textBytes += wmItemsSize(&records[at]);
      if ((kind == WIN_WIDGET_MENU_BAR)
        && (wmCountMenus(wmRecordText(&records[at]),
          wmItemsSize(&records[at])) < 0)
      ) {
        return WIN_ERR_INVALID;
      }
    } else if (kind != WIN_WIDGET_CANVAS) {
      textBytes += *wmRecordText(&records[at]);
    }
    at += length;
  }
  if (oldCount + count > WIN_MAX_WIDGETS) {
    return WIN_ERR_NO_MEMORY;
  }

  uint32_t bytes = sizeof(WmWidgets)
    + (uint32_t) (oldCount + count - 1) * sizeof(WmWidget) + textBytes;
  if (old != NULL) {
    bytes += oldBytes - sizeof(WmWidgets)
      - (uint32_t) (oldCount - 1) * sizeof(WmWidget);
  }
  if ((bytes > UINT16_MAX)
    || !wmCharge(state, window->owner, (int32_t) bytes - oldBytes)
  ) {
    return WIN_ERR_NO_MEMORY;
  }
  WmWidgets *widgets = (WmWidgets*) malloc(sizeof(WmWidgets)
    + (size_t) (oldCount + count - 1) * sizeof(WmWidget));
  if (widgets == NULL) {
    wmCharge(state, window->owner, (int32_t) oldBytes - (int32_t) bytes);
    return WIN_ERR_NO_MEMORY;
  }
  widgets->count = oldCount;
  widgets->focus = WM_NO_WIDGET;
  widgets->pressed = WM_NO_WIDGET;
  widgets->pressInside = false;
  if (old != NULL) {
    widgets->focus = old->focus;
    widgets->pressed = old->pressed;
    widgets->pressInside = old->pressInside;
    memcpy(widgets->widget, old->widget, oldCount * sizeof(WmWidget));
  }
  widgets->bytes = (uint16_t) bytes;

  GfxRect body;
  wmBody(window, &body);
  for (uint16_t at = 0; at < size; ) {
    const uint8_t *record = &records[at];
    WmWidget *widget = &widgets->widget[widgets->count];
    memset(widget, 0, sizeof(*widget));
    widget->kind = record[WIN_RECORD_KIND];
    widget->id = record[WIN_RECORD_ID];
    widget->flags = record[WIN_RECORD_FLAGS];
    widget->anchor = record[WIN_RECORD_ANCHOR];
    memcpy(&widget->rect, &record[WIN_RECORD_RECT], sizeof(widget->rect));
    widget->marginRight = body.w - widget->rect.x - widget->rect.w;
    widget->marginBottom = body.h - widget->rect.y - widget->rect.h;
    const uint8_t *fields = &record[WIN_RECORD_HEADER_SIZE];
    if (widget->kind == WIN_WIDGET_LABEL) {
      widget->align = fields[0];
    } else if (widget->kind == WIN_WIDGET_RADIO) {
      widget->group = fields[0];
      widget->value = fields[1];
    } else if (widget->kind == WIN_WIDGET_LIST) {
      widget->value = (uint16_t) (fields[0] | (fields[1] << 8));
    } else if ((_wmFields[widget->kind] > 0)
      && (widget->kind != WIN_WIDGET_TEXT_FIELD)
    ) {
      widget->value = fields[0];
    }
    const uint8_t *text = wmRecordText(record);
    const uint8_t *source = &text[1];
    uint16_t copy = 0;
    if (widget->kind == WIN_WIDGET_LIST) {
      source = text;
      widget->itemsSize = wmItemsSize(record);
      widget->numItems = wmCountItems((const char*) source,
        widget->itemsSize);
      if (widget->value >= widget->numItems) {
        widget->value = WIN_NO_ITEM;
      }
      wmShowSelection(window, widget);
      copy = widget->itemsSize;
    } else if (widget->kind == WIN_WIDGET_MENU_BAR) {
      source = text;
      widget->itemsSize = wmItemsSize(record);
      widget->numItems = (uint16_t) wmCountMenus(text, widget->itemsSize);
      copy = widget->itemsSize;
    } else if (widget->kind != WIN_WIDGET_CANVAS) {
      widget->textLength = *text;
    }
    if (widget->kind == WIN_WIDGET_TEXT_FIELD) {
      widget->maxLength = wmMaxLength(record);
      if (widget->textLength > widget->maxLength) {
        widget->textLength = widget->maxLength;
      }
      widget->caret = widget->textLength;
    }
    if (wmTextStorage(widget) > 0) {
      widget->text = (char*) malloc(wmTextStorage(widget));
      if (widget->text == NULL) {
        wmFreeWidgetTexts(widgets, oldCount);
        free(widgets);
        wmCharge(state, window->owner, (int32_t) oldBytes - (int32_t) bytes);
        return WIN_ERR_NO_MEMORY;
      }
      memcpy(widget->text, source, copy + widget->textLength);
    }
    widgets->count++;
    at += wmRecordSize(record, size - at, &status);
  }
  free(old);
  window->widgets = widgets;
  return WIN_OK;
}

/// @fn void* wmTemplateAdd(void *args)
///
/// @brief Add a template to the job's window:  one text grid record, or
/// widgets.
///
/// @param args A pointer to the WmTemplateJob, whose status is set.
///
/// @return Always returns the provided pointer.
void* wmTemplateAdd(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  if ((job->widgets != NULL) && (job->size == WIN_RECORD_HEADER_SIZE)
    && (job->widgets[WIN_RECORD_KIND] == WIN_WIDGET_TEXT_GRID)
  ) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Resize", "wmResizeMakeGrid",
      job);
  } else {
    job->status = wmAddTemplate(job->state, job->window, job->widgets,
      job->size);
  }
  return args;
}

/// @fn void* wmTemplateFree(void *args)
///
/// @brief Free the job's window's grid and widgets, and refund the widgets
/// to its owner.
///
/// @param args A pointer to the WmTemplateJob.
///
/// @return Always returns the provided pointer.
void* wmTemplateFree(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  if (job->window->content != NULL) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Resize", "wmResizeFreeGrid",
      job);
  }
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  if (widgets != NULL) {
    WmCanvasJob discard = { job->state, widgets, 0, widgets->count };
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Canvas", "wmCanvasDiscard",
      &discard);
    wmCharge(job->state, job->window->owner, -(int32_t) widgets->bytes);
    wmFreeWidgetTexts(widgets, 0);
    free(widgets);
    job->window->widgets = NULL;
  }
  return args;
}

/// @fn void* wmTemplateRequest(void *args)
///
/// @brief Carry out the job's widget request on a window of its sender's.
///
/// @param args A pointer to the WmTemplateJob, whose type, data, pid and
///   waiting are set.
///
/// @return Always returns the provided pointer.
void* wmTemplateRequest(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  WmState *state = job->state;
  // Every waited widget request's arguments start with its status and window.
  uint8_t *status = (uint8_t*) job->data;
  WmWindow *window = wmFind(state, ((uint8_t*) job->data)[1]);
  if (job->type == WIN_MSG_REMOVE_WIDGET) {
    WinWidgetArgs *remove = (WinWidgetArgs*) job->data;
    window = wmFind(state, remove->window);
    if ((window != NULL) && (window->owner == job->pid)) {
      job->window = window;
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Property",
        "wmPropertyRequest", job);
    }
    return args;
  }
  if (window == NULL) {
    *status = WIN_ERR_NOT_FOUND;
  } else if (window->owner != job->pid) {
    *status = WIN_ERR_PERMISSION;
  } else if (job->type == WIN_MSG_ADD_WIDGETS) {
    WinAddWidgetsArgs *add = (WinAddWidgetsArgs*) job->data;
    add->status = wmAddTemplate(state, window, add->widgets,
      add->templateSize);
    if (add->status == WIN_OK) {
      wmDamage(state, &window->frame);
    }
  } else if ((job->type == WIN_MSG_SET_WIDGET)
    || (job->type == WIN_MSG_GET_WIDGET)
    || (job->type == WIN_MSG_SET_CANVAS)
  ) {
    job->window = window;
    bool canvas = (job->type == WIN_MSG_SET_CANVAS);
    callOverlayFunction(OVERLAY_SAME_NAMESPACE,
      canvas ? "Canvas" : "Property",
      canvas ? "wmCanvasSet" : "wmPropertyRequest", job);
  } else {
    *status = WIN_ERR_NOT_SUPPORTED;
  }
  return args;
}
