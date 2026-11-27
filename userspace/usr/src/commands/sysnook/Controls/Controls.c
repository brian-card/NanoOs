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




/// @file Controls.c
///
/// @brief What a press, the pointer while a widget is held, or a key does to
/// a window's widgets, entered from the Input overlay only for windows that
/// have widgets.

#include "Wm.h"

/// @fn static bool wmClickable(const WmWidget *widget)
///
/// @brief Determine whether a widget takes presses and Space:  a push,
/// toggle or radio button or a check box that is enabled and shown.
///
/// @param widget The widget.
///
/// @return Returns true if it does, false otherwise.
static bool wmClickable(const WmWidget *widget) {
  return ((widget->kind == WIN_WIDGET_BUTTON)
      || (widget->kind == WIN_WIDGET_TOGGLE)
      || (widget->kind == WIN_WIDGET_CHECK_BOX)
      || (widget->kind == WIN_WIDGET_RADIO))
    && ((widget->flags & (WIN_WIDGET_DISABLED | WIN_WIDGET_HIDDEN)) == 0);
}

/// @fn static bool wmTakesPress(const WmWidget *widget)
///
/// @brief Determine whether a press on a widget is its own:  one that
/// takes presses, or a text field, list, menu bar or canvas that is
/// enabled and shown.
///
/// @param widget The widget.
///
/// @return Returns true if it is, false otherwise.
static bool wmTakesPress(const WmWidget *widget) {
  return wmClickable(widget)
    || (((widget->kind == WIN_WIDGET_TEXT_FIELD)
        || (widget->kind == WIN_WIDGET_LIST)
        || (widget->kind == WIN_WIDGET_MENU_BAR)
        || (widget->kind == WIN_WIDGET_CANVAS))
      && ((widget->flags & (WIN_WIDGET_DISABLED | WIN_WIDGET_HIDDEN)) == 0));
}

/// @fn static bool wmPass(WmInputJob *job, WmWidget *widget,
///   const char *overlay, const char *function)
///
/// @brief Have the Lists or Menus overlay carry out the job's event on a
/// list or menu bar.
///
/// @param job The job.
/// @param widget The list or menu bar, or NULL if there is none.
/// @param overlay "Lists" or "Menus".
/// @param function "wmListsInput" or "wmMenusInput".
///
/// @return Returns true if the widget used the event, false otherwise.
static bool wmPass(WmInputJob *job, WmWidget *widget, const char *overlay,
  const char *function
) {
  WmWidgetJob pass = { job->state, job->window, widget, { 0, 0, 0, 0 },
    job->event, false, false };

  if (widget != NULL) {
    wmWidgetRect(job->window, widget, &pass.rect);
  }

  callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, function, &pass);

  return pass.handled;
}

/// @fn static bool wmList(WmInputJob *job, WmWidget *widget)
///
/// @brief Have the Lists overlay carry out the job's event on a list.
///
/// @param job The job.
/// @param widget The list.
///
/// @return Returns true if the list used the event, false otherwise.
static bool wmList(WmInputJob *job, WmWidget *widget) {
  return wmPass(job, widget, "Lists", "wmListsInput");
}

/// @fn static bool wmMenus(WmInputJob *job, WmWidget *bar)
///
/// @brief Have the Menus overlay carry out the job's event on a menu bar
/// and its open menu.
///
/// @param job The job.
/// @param bar The menu bar, or NULL if there is none.
///
/// @return Returns true if the bar or menu used the event, false otherwise.
static bool wmMenus(WmInputJob *job, WmWidget *bar) {
  return wmPass(job, bar, "Menus", "wmMenusInput");
}

/// @fn static WmWidget* wmBar(const WmWindow *window, uint8_t index)
///
/// @brief Find a window's menu bar:  the widget at an index if it is one,
/// or else its first.
///
/// @param window The window, or NULL.
/// @param index The index to look at first.
///
/// @return Returns the menu bar, or NULL if the window has none.
static WmWidget* wmBar(const WmWindow *window, uint8_t index) {
  WmWidgets *widgets = (window != NULL) ? (WmWidgets*) window->widgets
    : NULL;
  if (widgets == NULL) {
    return NULL;
  } else if ((index < widgets->count)
    && (widgets->widget[index].kind == WIN_WIDGET_MENU_BAR)
  ) {
    return &widgets->widget[index];
  }

  for (uint8_t ii = 0; ii < widgets->count; ii++) {
    if (widgets->widget[ii].kind == WIN_WIDGET_MENU_BAR) {
      return &widgets->widget[ii];
    }
  }

  return NULL;
}

/// @fn static uint8_t wmControlAt(const WmWindow *window,
///   const WmWidgets *widgets, int16_t pointX, int16_t pointY)
///
/// @brief Find the widget a point is on whose press it is, the last in the
/// template if more than one, since it is painted over the others.
///
/// @param window The window.
/// @param widgets The window's widgets.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns the widget's index, or WM_NO_WIDGET.
static uint8_t wmControlAt(const WmWindow *window, const WmWidgets *widgets,
  int16_t pointX, int16_t pointY
) {
  for (uint8_t ii = widgets->count; ii > 0; ii--) {
    const WmWidget *widget = &widgets->widget[ii - 1];
    GfxRect rect;

    wmWidgetRect(window, widget, &rect);

    if (wmTakesPress(widget) && wmContains(&rect, pointX, pointY)) {
      return ii - 1;
    }
  }

  return WM_NO_WIDGET;
}

/// @fn static void wmClick(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, WmWidget *widget)
///
/// @brief Carry out a click on a widget, and tell its owner with
/// WIN_EVENT_COMMAND:  a push button's value is 0, a toggle button or check
/// box changes its value, and a radio button is set and the others of its
/// group cleared.
///
/// @param state The window manager's state.
/// @param window The widget's window.
/// @param widgets The window's widgets.
/// @param widget The widget.
///
/// @return This function returns no value.
static void wmClick(WmState *state, const WmWindow *window,
  WmWidgets *widgets, WmWidget *widget
) {
  wmReleaseChanged(state);

  if ((widget->kind == WIN_WIDGET_TOGGLE)
    || (widget->kind == WIN_WIDGET_CHECK_BOX)
  ) {
    widget->value = (widget->value != 0) ? 0 : 1;
  } else if (widget->kind == WIN_WIDGET_RADIO) {
    widget->value = 1;

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

  wmDamageWidget(state, window, widget);
  WinEvent *event = wmSend(state, window->owner, WIN_EVENT_COMMAND,
    window->id);
  if (event != NULL) {
    event->widget = widget->id;
    event->value = widget->value;
  }
}

/// @fn static void wmSetFocus(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, uint8_t index)
///
/// @brief Give a widget the keyboard focus, repainting it and the one that
/// had it.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param widgets The window's widgets.
/// @param index The widget's index.
///
/// @return This function returns no value.
static void wmSetFocus(WmState *state, const WmWindow *window,
  WmWidgets *widgets, uint8_t index
) {
  wmReleaseChanged(state);

  if (widgets->focus < widgets->count) {
    wmDamageWidget(state, window, &widgets->widget[widgets->focus]);
  }

  widgets->focus = index;
  wmDamageWidget(state, window, &widgets->widget[index]);
}

/// @fn static bool wmEdit(WmState *state, const WmWindow *window,
///   WmWidget *widget, const GfxEvent *event)
///
/// @brief Carry out a key on a text field going down:  a printable key
/// typed before the caret while the text is under its most, Backspace and
/// Delete, Left, Right, Home and End, and Enter, which sends
/// WIN_EVENT_COMMAND.  A read-only field's text doesn't change.
///
/// @param state The window manager's state.
/// @param window The field's window.
/// @param widget The text field.
/// @param event The key.
///
/// @return Returns true if the field uses the key, false otherwise.
static bool wmEdit(WmState *state, const WmWindow *window, WmWidget *widget,
  const GfxEvent *event
) {
  uint8_t key = event->keycode;
  bool printable = (event->ascii >= ' ') && (event->ascii < 0x7F);
  bool editable = (widget->flags & WIN_WIDGET_READ_ONLY) == 0;
  uint8_t length = widget->textLength;
  uint8_t caret = widget->caret;

  if ((!printable) && (key != GFX_KEY_BACKSPACE) && (key != GFX_KEY_DELETE)
    && (key != GFX_KEY_LEFT) && (key != GFX_KEY_RIGHT)
    && (key != GFX_KEY_HOME) && (key != GFX_KEY_END)
    && (key != GFX_KEY_ENTER)
  ) {
    return false;
  } else if (event->down == 0) {
    return true;
  }
  if (printable && editable && (length < widget->maxLength)) {
    memmove(&widget->text[caret + 1], &widget->text[caret], length - caret);
    widget->text[caret] = (char) event->ascii;
    widget->textLength++;
    widget->caret++;
  } else if ((key == GFX_KEY_BACKSPACE) && editable && (caret > 0)) {
    memmove(&widget->text[caret - 1], &widget->text[caret], length - caret);
    widget->textLength--;
    widget->caret--;
  } else if ((key == GFX_KEY_DELETE) && editable && (caret < length)) {
    memmove(&widget->text[caret], &widget->text[caret + 1],
      length - caret - 1);
    widget->textLength--;
  } else if ((key == GFX_KEY_LEFT) && (caret > 0)) {
    widget->caret--;
  } else if ((key == GFX_KEY_RIGHT) && (caret < length)) {
    widget->caret++;
  } else if (key == GFX_KEY_HOME) {
    widget->caret = 0;
  } else if (key == GFX_KEY_END) {
    widget->caret = length;
  } else if (key == GFX_KEY_ENTER) {
    wmReleaseChanged(state);
    WinEvent *command = wmSend(state, window->owner, WIN_EVENT_COMMAND,
      window->id);
    if (command != NULL) {
      command->widget = widget->id;
    }
  }

  widget->scroll = wmFieldScroll(state, window, widget);
  wmDamageWidget(state, window, widget);

  if ((state->times != NULL) && (!state->echoKey)) {
    timespec_get(&state->keySent, TIME_UTC);
    state->echoKey = true;
  }

  state->gridPainted = true;

  if (widget->textLength != length) {
    if ((state->heldWindow != 0) && ((state->heldWindow != window->id)
      || (state->heldWidget != widget->id))
    ) {
      wmReleaseChanged(state);
    }

    state->heldWindow = window->id;
    state->heldWidget = widget->id;
    timespec_get(&state->heldSince, TIME_UTC);
    state->eventsWaiting = true;
  }

  return true;
}

/// @fn static void wmCanvasPointer(WmState *state, const WmWindow *window,
///   const WmWidget *canvas, const GfxEvent *event)
///
/// @brief Tell a canvas's owner where the pointer is, in the canvas's
/// coordinates, with WIN_EVENT_POINTER.
///
/// @param state The window manager's state.
/// @param window The canvas's window.
/// @param canvas The canvas.
/// @param event The pointer event.
///
/// @return This function returns no value.
static void wmCanvasPointer(WmState *state, const WmWindow *window,
  const WmWidget *canvas, const GfxEvent *event
) {
  GfxRect rect;
  wmWidgetRect(window, canvas, &rect);
  WinEvent *sent = wmSend(state, window->owner, WIN_EVENT_POINTER,
    window->id);
  if (sent != NULL) {
    sent->widget = canvas->id;
    sent->x = event->x - rect.x;
    sent->y = event->y - rect.y;
    sent->buttons = event->buttons;
  }
}

/// @fn void* wmControlsPress(void *args)
///
/// @brief Start holding the widget a press lands on, if it takes presses,
/// giving it the focus.  The press is the job's event.
///
/// @param args A pointer to the WmInputJob, whose handled is set if a
///   widget was pressed.
///
/// @return Always returns the provided pointer.
void* wmControlsPress(void *args) {
  WmInputJob *job = (WmInputJob*) args;
  WmState *state = job->state;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  uint8_t index = wmControlAt(job->window, widgets, job->event->x,
    job->event->y);

  job->handled = (index != WM_NO_WIDGET);
  if (!job->handled) {
    return args;
  }

  WmWidget *widget = &widgets->widget[index];
  if (widget->kind == WIN_WIDGET_MENU_BAR) {
    wmMenus(job, widget);
    return args;
  } else if (widget->kind == WIN_WIDGET_CANVAS) {
    wmCanvasPointer(state, job->window, widget, job->event);
  } else if (widget->kind == WIN_WIDGET_LIST) {
    wmSetFocus(state, job->window, widgets, index);
    wmList(job, widget);
    return args;
  } else if (widget->kind == WIN_WIDGET_TEXT_FIELD) {
    GfxRect rect;
    wmWidgetRect(job->window, widget, &rect);
    uint8_t glyphW = state->display.builtInFont.glyphW;
    int16_t at = wmFieldScroll(state, job->window, widget)
      + ((job->event->x - rect.x - WM_FIELD_INSET + (glyphW / 2)) / glyphW);

    if (at < 0) {
      at = 0;
    } else if (at > widget->textLength) {
      at = widget->textLength;
    }

    widget->caret = (uint8_t) at;
    wmSetFocus(state, job->window, widgets, index);
    return args;
  }

  widgets->pressed = index;
  widgets->pressInside = true;
  wmSetFocus(state, job->window, widgets, index);
  state->grab = WM_GRAB_WIDGET;
  state->grabbed = job->window->id;

  return args;
}

/// @fn void* wmControlsPointer(void *args)
///
/// @brief Follow the pointer while a widget is held:  whether it is still
/// on the widget and, once the button is released, a click if it is.
///
/// @param args A pointer to the WmInputJob, whose window is the grabbed
///   one, or NULL if it has gone.
///
/// @return Always returns the provided pointer.
void* wmControlsPointer(void *args) {
  WmInputJob *job = (WmInputJob*) args;
  WmState *state = job->state;

  if (state->grab == WM_GRAB_MENU) {
    wmMenus(job, wmBar(job->window, state->menu.bar));
    return args;
  }

  WmWidgets *widgets = (job->window != NULL)
    ? (WmWidgets*) job->window->widgets : NULL;
  bool down = (job->event->buttons & GFX_BUTTON_LEFT) != 0;

  if (!down) {
    state->grab = WM_GRAB_NONE;
    state->grabbed = 0;
  }

  if ((widgets == NULL) || (widgets->pressed >= widgets->count)) {
    return args;
  }

  WmWidget *widget = &widgets->widget[widgets->pressed];
  GfxRect rect;

  wmWidgetRect(job->window, widget, &rect);
  bool inside = wmContains(&rect, job->event->x, job->event->y);

  if (widget->kind == WIN_WIDGET_CANVAS) {
    wmCanvasPointer(state, job->window, widget, job->event);
    inside = widgets->pressInside;
  }

  if (!down) {
    widgets->pressed = WM_NO_WIDGET;
    widgets->pressInside = false;
    wmDamageWidget(state, job->window, widget);

    if (inside && wmClickable(widget)) {
      wmClick(state, job->window, widgets, widget);
    }
  } else if (inside != widgets->pressInside) {
    widgets->pressInside = inside;
    wmDamageWidget(state, job->window, widget);
  }

  return args;
}

/// @fn static bool wmFocusable(const WmWidget *widget)
///
/// @brief Determine whether a widget takes the focus:  a button, box, text
/// field or list that is enabled and shown.
///
/// @param widget The widget.
///
/// @return Returns true if it does, false otherwise.
static bool wmFocusable(const WmWidget *widget) {
  return wmTakesPress(widget) && (widget->kind != WIN_WIDGET_MENU_BAR);
}

/// @fn static void wmTab(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, int8_t step)
///
/// @brief Move the focus to the next widget that takes it, in template
/// order, wrapping.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param widgets The window's widgets.
/// @param step 1 for the next, -1 for the one before.
///
/// @return This function returns no value.
static void wmTab(WmState *state, const WmWindow *window, WmWidgets *widgets,
  int8_t step
) {
  int16_t count = widgets->count;
  int16_t at = widgets->focus;

  if (at >= count) {
    at = (step > 0) ? -1 : count;
  }

  for (int16_t tries = 0; tries < count; tries++) {
    at += step;

    if (at < 0) {
      at = count - 1;
    } else if (at >= count) {
      at = 0;
    }
    if (wmFocusable(&widgets->widget[at])) {
      wmSetFocus(state, window, widgets, (uint8_t) at);
      return;
    }
  }
}

/// @fn static WmWidget* wmFlagged(WmWidgets *widgets, uint8_t flag)
///
/// @brief Find the push button with a flag that can be clicked:  the
/// default or the cancel button.
///
/// @param widgets The window's widgets.
/// @param flag WIN_WIDGET_DEFAULT or WIN_WIDGET_CANCEL.
///
/// @return Returns the button, or NULL if there is none.
static WmWidget* wmFlagged(WmWidgets *widgets, uint8_t flag) {
  for (uint8_t ii = 0; ii < widgets->count; ii++) {
    WmWidget *widget = &widgets->widget[ii];
    if (((widget->flags & flag) != 0) && wmClickable(widget)) {
      return widget;
    }
  }

  return NULL;
}

/// @fn static bool wmFocusedKey(WmInputJob *job, WmWidgets *widgets)
///
/// @brief Give the focused widget a key it uses:  a text field's editing
/// keys, a list's keys, or Space, or Enter on a push or toggle button,
/// which clicks a button or box when it goes down.
///
/// @param job The job.
/// @param widgets The window's widgets.
///
/// @return Returns true if the widget used the key, false otherwise.
static bool wmFocusedKey(WmInputJob *job, WmWidgets *widgets) {
  const GfxEvent *event = job->event;

  if (widgets->focus >= widgets->count) {
    return false;
  }

  WmWidget *widget = &widgets->widget[widgets->focus];
  bool usable
    = (widget->flags & (WIN_WIDGET_DISABLED | WIN_WIDGET_HIDDEN)) == 0;
  bool enter = (event->keycode == GFX_KEY_ENTER);

  if ((widget->kind == WIN_WIDGET_TEXT_FIELD) && usable) {
    if (enter && (event->down != 0)
      && (wmFlagged(widgets, WIN_WIDGET_DEFAULT) != NULL)
    ) {
      return false;
    }

    return wmEdit(job->state, job->window, widget, event);
  } else if ((widget->kind == WIN_WIDGET_LIST) && usable) {
    return wmList(job, widget);
  } else if ((event->keycode != ' ')
    && (!(enter && ((widget->kind == WIN_WIDGET_BUTTON)
      || (widget->kind == WIN_WIDGET_TOGGLE))))
  ) {
    return false;
  } else if (!wmClickable(widget)) {
    return false;
  } else if (event->down != 0) {
    wmClick(job->state, job->window, widgets, widget);
  }

  return true;
}

/// @fn void* wmControlsKey(void *args)
///
/// @brief Carry out a key on a window with widgets:  the menu bar's keys
/// first, for a key that could open, close or drive a menu, then Tab and
/// Shift+Tab, which move the focus, then the focused widget's, and then Enter
/// and Escape, which click the default and the cancel button.  The release
/// of a key the widgets took the press of is the Input overlay's to drop.
///
/// @param args A pointer to the WmInputJob, whose handled is set if a
///   widget used the key.
///
/// @return Always returns the provided pointer.
void* wmControlsKey(void *args) {
  WmInputJob *job = (WmInputJob*) args;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  const GfxEvent *event = job->event;
  uint8_t key = event->keycode;
  WmMenuState *menu = &job->state->menu;
  bool menuKey = menu->open || menu->altAlone || (menu->swallow != 0)
    || ((event->modifiers & WM_MOD_ALT) != 0) || (key == GFX_KEY_LEFT_ALT)
    || (key == GFX_KEY_RIGHT_ALT) || (key == GFX_KEY_F10);
  WmWidget *bar = menuKey ? wmBar(job->window, menu->bar) : NULL;

  if (((bar != NULL) || menu->open) && wmMenus(job, bar)) {
    job->handled = true;
    return args;
  }

  WmWidget *button = NULL;

  if ((key == GFX_KEY_TAB) && ((event->modifiers & WM_MOD_ALT) == 0)) {
    job->handled = true;

    if (event->down != 0) {
      wmTab(job->state, job->window, widgets,
        ((event->modifiers & GFX_MOD_SHIFT) != 0) ? -1 : 1);
    }
  } else if (wmFocusedKey(job, widgets)) {
    job->handled = true;
  } else if (key == GFX_KEY_ENTER) {
    button = wmFlagged(widgets, WIN_WIDGET_DEFAULT);
  } else if (key == GFX_KEY_ESCAPE) {
    button = wmFlagged(widgets, WIN_WIDGET_CANCEL);
  }

  if (button != NULL) {
    job->handled = true;

    if (event->down != 0) {
      wmClick(job->state, job->window, widgets, button);
    }
  }

  if (job->handled && (event->down != 0)) {
    job->state->widgetKey = key;
  }

  return args;
}

/// @fn void* wmControlsWheel(void *args)
///
/// @brief Give the wheel to the list the pointer is on, if it is on one.
///
/// @param args A pointer to the WmInputJob.
///
/// @return Always returns the provided pointer.
void* wmControlsWheel(void *args) {
  WmInputJob *job = (WmInputJob*) args;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  uint8_t index = wmControlAt(job->window, widgets, job->event->x,
    job->event->y);

  if ((index != WM_NO_WIDGET)
    && (widgets->widget[index].kind == WIN_WIDGET_LIST)
  ) {
    job->handled = wmList(job, &widgets->widget[index]);
  }

  return args;
}
