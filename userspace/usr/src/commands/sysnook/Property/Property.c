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




/// @file Property.c
///
/// @brief The widget requests that change a window's widgets or read them:
/// removing a widget, and setting or getting one of its properties.

#include "Wm.h"

/// @fn static uint8_t wmSetText(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, WmWidget *widget, const uint8_t *data,
///   uint16_t size)
///
/// @brief Replace a widget's text, charging its owner for the difference.  A
/// text field's text is cut to its most and goes in its storage, with the
/// caret at its end.
///
/// @param state The window manager's state.
/// @param window The widget's window.
/// @param widgets The window's widgets.
/// @param widget The widget.
/// @param data The new text, with no terminator.
/// @param size The bytes at data.
///
/// @return Returns WIN_OK, WIN_ERR_INVALID for a text over 255 bytes, or
/// WIN_ERR_NO_MEMORY, in which case the widget keeps its text.
static uint8_t wmSetText(WmState *state, const WmWindow *window,
  WmWidgets *widgets, WmWidget *widget, const uint8_t *data, uint16_t size
) {
  if ((size > UINT8_MAX) || ((size > 0) && (data == NULL))) {
    return WIN_ERR_INVALID;
  } else if (widget->kind == WIN_WIDGET_TEXT_FIELD) {
    if (size > widget->maxLength) {
      size = widget->maxLength;
    }

    memcpy(widget->text, data, size);
    widget->textLength = (uint8_t) size;
    widget->caret = widget->textLength;
    return WIN_OK;
  }

  int32_t change = ((int32_t) size) - widget->textLength;
  if (((((uint32_t) widgets->bytes) + change) > UINT16_MAX)
    || (!wmCharge(state, window->owner, change))
  ) {
    return WIN_ERR_NO_MEMORY;
  }

  char *text = NULL;

  if (size > 0) {
    text = (char*) malloc(size);
    if (text == NULL) {
      wmCharge(state, window->owner, -change);
      return WIN_ERR_NO_MEMORY;
    }

    memcpy(text, data, size);
  }

  free(widget->text);
  widget->text = text;
  widget->textLength = (uint8_t) size;
  widgets->bytes = (uint16_t) (widgets->bytes + change);

  return WIN_OK;
}

/// @fn static uint8_t wmSetItems(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, WmWidget *list, const uint8_t *data, uint16_t size)
///
/// @brief Replace a list's items, charging its owner for the difference,
/// and keep its selection if there are still that many items, or else
/// select the last.
///
/// @param state The window manager's state.
/// @param window The list's window.
/// @param widgets The window's widgets.
/// @param list The list.
/// @param data The new items.
/// @param size The bytes at data.
///
/// @return Returns WIN_OK, WIN_ERR_INVALID for items over
/// WIN_LIST_ITEMS_MAX bytes, or WIN_ERR_NO_MEMORY, in which case the list
/// keeps its items.
static uint8_t wmSetItems(WmState *state, const WmWindow *window,
  WmWidgets *widgets, WmWidget *list, const uint8_t *data, uint16_t size
) {
  if ((size > WIN_LIST_ITEMS_MAX) || ((size > 0) && (data == NULL))) {
    return WIN_ERR_INVALID;
  }

  int32_t change = ((int32_t) size) - list->itemsSize;
  if (((((uint32_t) widgets->bytes) + change) > UINT16_MAX)
    || (!wmCharge(state, window->owner, change))
  ) {
    return WIN_ERR_NO_MEMORY;
  }

  char *items = NULL;

  if (size > 0) {
    items = (char*) malloc(size);
    if (items == NULL) {
      wmCharge(state, window->owner, -change);
      return WIN_ERR_NO_MEMORY;
    }

    memcpy(items, data, size);
  }

  free(list->text);
  list->text = items;
  list->itemsSize = size;
  list->numItems = wmCountItems(items, size);
  widgets->bytes = (uint16_t) (widgets->bytes + change);

  if ((list->value != WIN_NO_ITEM) && (list->value >= list->numItems)) {
    list->value = (list->numItems > 0) ? (uint16_t) (list->numItems - 1)
      : WIN_NO_ITEM;
  }

  wmShowSelection(window, list);

  return WIN_OK;
}

/// @fn static uint8_t wmSetMenuItem(WmWidget *bar, uint16_t value)
///
/// @brief Set a menu bar item's WIN_MENU_DISABLED and WIN_MENU_CHECKED bits.
///
/// @param bar The menu bar.
/// @param value The item's identifier in the high byte and the bits in the
///   low byte.
///
/// @return Returns WIN_OK, or WIN_ERR_NOT_FOUND if no item has the
/// identifier.
static uint8_t wmSetMenuItem(WmWidget *bar, uint16_t value) {
  uint8_t id = (uint8_t) (value >> 8);
  uint8_t bits = WIN_MENU_DISABLED | WIN_MENU_CHECKED;

  for (uint8_t menu = 0; (id != 0) && (menu < bar->numItems); menu++) {
    uint8_t *title = wmMenu(bar, menu);
    uint8_t count = *wmSkipText(title);

    for (uint8_t ii = 0; ii < count; ii++) {
      uint8_t *item = wmMenuItem(title, ii);
      if (item[0] == id) {
        item[1] = (uint8_t) ((item[1] & (~bits)) | (value & bits));
        return WIN_OK;
      }
    }
  }

  return WIN_ERR_NOT_FOUND;
}

/// @fn static void wmSetWidget(WmState *state, WmWindow *window,
///   WinSetWidgetArgs *args)
///
/// @brief Carry out a WIN_MSG_SET_WIDGET on a widget of the sender's window,
/// and mark where the widget was and is to be painted again.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param args The request's arguments.
///
/// @return This function returns no value.
static void wmSetWidget(WmState *state, WmWindow *window,
  WinSetWidgetArgs *args
) {
  WmWidgets *widgets = (WmWidgets*) window->widgets;
  WmWidget *widget = (args->widget != WIN_NONE)
    ? wmWidgetById(widgets, args->widget) : NULL;

  if (widget == NULL) {
    args->status = WIN_ERR_NOT_FOUND;
    return;
  }

  wmDamageWidget(state, window, widget);
  args->status = WIN_OK;
  bool list = (widget->kind == WIN_WIDGET_LIST);

  if ((args->property == WIN_PROP_MENU_ITEM)
    && (widget->kind == WIN_WIDGET_MENU_BAR)
  ) {
    args->status = wmSetMenuItem(widget, args->value);

    if (state->menu.open && (state->menu.window == window->id)) {
      wmDamage(state, &state->menu.rect);
    }
  } else if ((args->property == WIN_PROP_ITEMS) && list) {
    args->status = wmSetItems(state, window, widgets, widget, args->data,
      args->size);
  } else if ((args->property == WIN_PROP_VALUE) && list) {
    if ((args->value != WIN_NO_ITEM) && (args->value >= widget->numItems)) {
      args->status = WIN_ERR_INVALID;
    } else {
      widget->value = args->value;
      wmShowSelection(window, widget);
    }
  } else if ((args->property == WIN_PROP_TEXT) && (!list)
    && (widget->kind != WIN_WIDGET_MENU_BAR)
  ) {
    args->status = wmSetText(state, window, widgets, widget, args->data,
      args->size);
  } else if (args->property == WIN_PROP_ENABLED) {
    widget->flags &= (uint8_t) ~WIN_WIDGET_DISABLED;

    if (args->value == 0) {
      widget->flags |= WIN_WIDGET_DISABLED;
    }
  } else if (args->property == WIN_PROP_HIDDEN) {
    widget->flags &= (uint8_t) ~WIN_WIDGET_HIDDEN;

    if (args->value != 0) {
      widget->flags |= WIN_WIDGET_HIDDEN;
    }
  } else if (args->property == WIN_PROP_VALUE) {
    widget->value = args->value;

    if ((widget->kind == WIN_WIDGET_RADIO) && (args->value != 0)) {
      for (uint8_t ii = 0; ii < widgets->count; ii++) {
        WmWidget *other = &widgets->widget[ii];
        if ((other != widget) && (other->kind == WIN_WIDGET_RADIO)
          && (other->group == widget->group) && (other->value != 0)
        ) {
          other->value = 0;
          wmDamageWidget(state, window, other);
        }
      }
    }
  } else if (args->property == WIN_PROP_FOCUS) {
    if (widgets->focus < widgets->count) {
      wmDamageWidget(state, window, &widgets->widget[widgets->focus]);
    }

    widgets->focus = (uint8_t) (widget - widgets->widget);
  } else if (args->property == WIN_PROP_RECT) {
    GfxRect body;
    wmBody(window, &body);
    widget->rect = args->rect;
    widget->marginRight = body.w - widget->rect.x - widget->rect.w;
    widget->marginBottom = body.h - widget->rect.y - widget->rect.h;

    if (list) {
      wmShowSelection(window, widget);
    }
  } else {
    args->status = WIN_ERR_NOT_SUPPORTED;
  }

  wmDamageWidget(state, window, widget);
}

/// @fn static void wmGetWidget(WmWindow *window, WinGetWidgetArgs *args)
///
/// @brief Carry out a WIN_MSG_GET_WIDGET on a widget of the sender's window.
///
/// @param window The window.
/// @param args The request's arguments.
///
/// @return This function returns no value.
static void wmGetWidget(WmWindow *window, WinGetWidgetArgs *args) {
  WmWidget *widget = (args->widget != WIN_NONE)
    ? wmWidgetById((WmWidgets*) window->widgets, args->widget) : NULL;
  if (widget == NULL) {
    args->status = WIN_ERR_NOT_FOUND;
  } else if (args->property == WIN_PROP_TEXT) {
    const char *text = widget->text;
    uint16_t whole = widget->textLength;

    if (widget->kind == WIN_WIDGET_LIST) {
      whole = 0;

      if (widget->value != WIN_NO_ITEM) {
        text = wmItem(widget, widget->value, &whole);
      }
    }

    uint16_t length = whole;
    if (length > args->capacity) {
      length = args->capacity;
    }
    if ((length > 0) && (args->data != NULL)) {
      memcpy(args->data, text, length);
    }

    args->length = whole;
    args->status = WIN_OK;
  } else if (args->property == WIN_PROP_VALUE) {
    args->value = widget->value;
    args->status = WIN_OK;
  } else {
    args->status = WIN_ERR_NOT_SUPPORTED;
  }
}

/// @fn static void wmRemoveWidget(WmState *state, WmWindow *window,
///   uint8_t id)
///
/// @brief Remove a widget from a window, refunding its text to the owner.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param id The widget's identifier.
///
/// @return This function returns no value.
static void wmRemoveWidget(WmState *state, WmWindow *window, uint8_t id) {
  WmWidgets *widgets = (WmWidgets*) window->widgets;
  WmWidget *widget = (id != WIN_NONE) ? wmWidgetById(widgets, id) : NULL;

  if (widget == NULL) {
    return;
  }

  wmDamageWidget(state, window, widget);
  uint8_t index = (uint8_t) (widget - widgets->widget);

  if (widget->kind == WIN_WIDGET_CANVAS) {
    WmCanvasJob discard = { state, widgets, index, (uint8_t) (index + 1) };
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Canvas", "wmCanvasDiscard",
      &discard);
  }

  wmCharge(state, window->owner, -(int32_t) wmTextStorage(widget));
  widgets->bytes = (uint16_t) (widgets->bytes - wmTextStorage(widget));
  free(widget->text);
  memmove(widget, widget + 1,
    ((size_t) (widgets->count - index - 1)) * sizeof(WmWidget));
  widgets->count--;
  uint8_t *indexes[] = { &widgets->focus, &widgets->pressed };
  for (uint8_t ii = 0; ii < (sizeof(indexes) / sizeof(indexes[0])); ii++) {
    if (*indexes[ii] == index) {
      *indexes[ii] = WM_NO_WIDGET;
    } else if ((*indexes[ii] != WM_NO_WIDGET) && (*indexes[ii] > index)) {
      (*indexes[ii])--;
    }
  }
}

/// @fn void* wmPropertyRequest(void *args)
///
/// @brief Carry out a WIN_MSG_REMOVE_WIDGET, WIN_MSG_SET_WIDGET or
/// WIN_MSG_GET_WIDGET on the job's window, which the Template overlay has
/// found and checked the sender owns.
///
/// @param args A pointer to the WmTemplateJob.
///
/// @return Always returns the provided pointer.
void* wmPropertyRequest(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  if (job->type == WIN_MSG_REMOVE_WIDGET) {
    wmRemoveWidget(job->state, job->window,
      ((WinWidgetArgs*) job->data)->widget);
  } else if (job->type == WIN_MSG_SET_WIDGET) {
    wmSetWidget(job->state, job->window, (WinSetWidgetArgs*) job->data);

  } else {
    wmGetWidget(job->window, (WinGetWidgetArgs*) job->data);
  }

  return args;
}

/// @fn static int16_t wmCountMenus(const uint8_t *menus, uint16_t size)
///
/// @brief Check a menu bar's menus and count them:  at most WIN_MAX_MENUS,
/// each with at most WIN_MAX_MENU_ITEMS items, all inside its size, and no
/// identifier but a separator's 0 used twice.
///
/// @param menus The menus.
/// @param size The bytes at menus.
///
/// @return Returns the number of menus, or -1 if they are malformed.
static int16_t wmCountMenus(const uint8_t *menus, uint16_t size) {
  uint8_t seen[(UINT8_MAX + 1) / 8];
  memset(seen, 0, sizeof(seen));
  const uint8_t *end = menus + size;
  int16_t count = 0;

  for (const uint8_t *at = menus; at < end; count++) {
    at = wmSkipText(at);
    if ((count == WIN_MAX_MENUS) || (at >= end) || (*at > WIN_MAX_MENU_ITEMS)) {
      return -1;
    }

    uint8_t numItems = *at++;
    for (uint8_t ii = 0; ii < numItems; ii++) {
      if ((end - at) < 4) {
        return -1;
      }

      uint8_t id = at[0];
      uint8_t bit = (uint8_t) (1 << (id & 7));

      if ((id != 0) && ((seen[id >> 3] & bit) != 0)) {
        return -1;
      }

      seen[id >> 3] |= bit;
      at = wmSkipText(at + 2);
      if ((at >= end) || ((at = wmSkipText(at)) > end)) {
        return -1;
      }
    }

    if (at > end) {
      return -1;
    }
  }

  return count;
}

/// @fn void* wmPropertyMenus(void *args)
///
/// @brief Check the job's menu bar menus and count them, for the Template
/// overlay.
///
/// @param args A pointer to the WmMenusCheck.
///
/// @return Always returns the provided pointer.
void* wmPropertyMenus(void *args) {
  WmMenusCheck *check = (WmMenusCheck*) args;
  check->count = wmCountMenus(check->menus, check->size);

  return args;
}
