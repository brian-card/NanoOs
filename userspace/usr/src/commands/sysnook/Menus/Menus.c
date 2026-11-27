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


/// @file Menus.c
///
/// @brief What the pointer and keys do to a menu bar and its open menu,
/// entered from the Controls overlay only for windows with a bar.

#include "WmMenus.h"

/// @def WM_LOWER_CASE
///
/// @brief The bit that makes an ASCII letter lower case.  ctype.h isn't
/// used, since on the simulator it brings the host's locale tables in.
#define WM_LOWER_CASE 0x20

/// @fn static uint8_t wmTitleAt(WmState *state, const WmWidget *bar,
///   const GfxRect *barRect, int16_t pointX, int16_t pointY)
///
/// @brief Find the menu bar's title a point is on.
///
/// @param state The window manager's state.
/// @param bar The menu bar.
/// @param barRect Where the bar is.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns the menu's index, or WM_NO_WIDGET.
static uint8_t wmTitleAt(WmState *state, const WmWidget *bar,
  const GfxRect *barRect, int16_t pointX, int16_t pointY
) {
  for (uint8_t ii = 0; ii < bar->numItems; ii++) {
    GfxRect rect;
    wmTitleRect(state, bar, barRect, ii, &rect);

    if (wmContains(&rect, pointX, pointY)) {
      return ii;
    }
  }

  return WM_NO_WIDGET;
}

/// @fn static bool wmUsable(const uint8_t *item)
///
/// @brief Determine whether a dropdown's item can be highlighted and
/// chosen:  one that is neither disabled nor a separator.
///
/// @param item The item.
///
/// @return Returns true if it can, false otherwise.
static bool wmUsable(const uint8_t *item) {
  return (item[1] & (WIN_MENU_DISABLED | WIN_MENU_SEPARATOR)) == 0;
}

/// @fn static void wmDropdownRect(WmState *state, const WmWidget *bar,
///   const GfxRect *barRect, uint8_t index, GfxRect *rect)
///
/// @brief Find where a menu's dropdown goes:  below its title, as wide as
/// its widest text and shortcut, moved left or up to stay on the screen.
///
/// @param state The window manager's state.
/// @param bar The menu bar.
/// @param barRect Where the bar is.
/// @param index The menu's index.
/// @param rect Where to store where the dropdown goes.
///
/// @return This function returns no value.
static void wmDropdownRect(WmState *state, const WmWidget *bar,
  const GfxRect *barRect, uint8_t index, GfxRect *rect
) {
  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t *menu = wmMenu(bar, index);
  uint8_t count = *wmSkipText(menu);
  uint8_t text = 0;
  uint8_t shortcut = 0;
  int16_t height = 2;

  for (uint8_t ii = 0; ii < count; ii++) {
    const uint8_t *item = wmMenuItem(menu, ii);
    const uint8_t *keys = wmSkipText(&item[2]);

    if (item[2] > text) {
      text = item[2];
    }

    if (*keys > shortcut) {
      shortcut = *keys;
    }

    height += wmItemHeight(item);
  }

  int16_t width = 2 + WM_CHECK_GUTTER + (text * glyphW) + WM_ITEM_PAD;

  if (shortcut > 0) {
    width += WM_SHORTCUT_GAP + (shortcut * glyphW);
  }

  wmTitleRect(state, bar, barRect, index, rect);
  rect->y = barRect->y + barRect->h;
  rect->w = width;
  rect->h = height;

  if ((rect->x + width) > ((int16_t) state->display.width)) {
    rect->x = ((int16_t) state->display.width) - width;
  }

  if ((rect->y + height) > ((int16_t) state->display.height)) {
    rect->y = ((int16_t) state->display.height) - height;
  }
}

/// @fn static uint8_t wmItemAt(const WmMenuState *open, uint8_t *menu,
///   int16_t pointY)
///
/// @brief Find the item of the open dropdown a row is on.
///
/// @param open The open menu.
/// @param menu The open menu's title length byte.
/// @param pointY The row.
///
/// @return Returns the item's index, or WM_NO_WIDGET.
static uint8_t wmItemAt(const WmMenuState *open, uint8_t *menu,
  int16_t pointY
) {
  int16_t top = open->rect.y + 1;
  uint8_t count = *wmSkipText(menu);

  for (uint8_t ii = 0; ii < count; ii++) {
    int16_t height = wmItemHeight(wmMenuItem(menu, ii));
    if ((pointY >= top) && (pointY < (top + height))) {
      return ii;
    }

    top += height;
  }

  return WM_NO_WIDGET;
}

/// @fn static uint8_t wmNextUsable(uint8_t *menu, uint8_t from,
///   int8_t step)
///
/// @brief Find the next item of a menu that can be highlighted, going one
/// way and wrapping.
///
/// @param menu The menu's title length byte.
/// @param from The index to start after, or WM_NO_WIDGET for none.
/// @param step 1 to go down, -1 to go up.
///
/// @return Returns the item's index, or WM_NO_WIDGET if none can be.
static uint8_t wmNextUsable(uint8_t *menu, uint8_t from, int8_t step) {
  int16_t count = *wmSkipText(menu);
  int16_t at = (from == WM_NO_WIDGET) ? ((step > 0) ? -1 : count) : from;

  for (int16_t tries = 0; tries < count; tries++) {
    at += step;

    if (at < 0) {
      at = count - 1;
    } else if (at >= count) {
      at = 0;
    }
    if (wmUsable(wmMenuItem(menu, (uint8_t) at))) {
      return (uint8_t) at;
    }
  }

  return WM_NO_WIDGET;
}

/// @fn static void wmClose(WmState *state)
///
/// @brief Close the open menu, repainting what its dropdown covered and its
/// bar.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmClose(WmState *state) {
  WmMenuState *open = &state->menu;
  WmWindow *window = wmFind(state, open->window);

  wmDamage(state, &open->rect);

  if ((window != NULL) && (window->widgets != NULL)
    && (open->bar < ((WmWidgets*) window->widgets)->count)
  ) {
    wmDamageWidget(state, window,
      &((WmWidgets*) window->widgets)->widget[open->bar]);
  }

  open->open = false;
  state->grab = WM_GRAB_NONE;
  state->grabbed = 0;
}

/// @fn static void wmOpen(WmWidgetJob *job, uint8_t index)
///
/// @brief Open one of the job's menu bar's menus, with no item highlighted,
/// closing any other, and hold the pointer until it closes.
///
/// @param job The job, whose widget is the menu bar.
/// @param index The menu's index.
///
/// @return This function returns no value.
static void wmOpen(WmWidgetJob *job, uint8_t index) {
  WmState *state = job->state;
  WmMenuState *open = &state->menu;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;

  if (open->open) {
    wmClose(state);
  }

  open->open = true;
  open->window = job->window->id;
  open->bar = (uint8_t) (job->widget - widgets->widget);
  open->menu = index;
  open->item = WM_NO_WIDGET;
  wmDropdownRect(state, job->widget, &job->rect, index, &open->rect);
  wmDamage(state, &open->rect);
  wmDamageWidget(state, job->window, job->widget);
  state->grab = WM_GRAB_MENU;
  state->grabbed = job->window->id;
}

/// @fn static void wmDamageItem(WmState *state, uint8_t *menu,
///   uint8_t item)
///
/// @brief Repaint one item of the open dropdown.
///
/// @param state The window manager's state.
/// @param menu The open menu's title length byte.
/// @param item The item's index, or WM_NO_WIDGET for none.
///
/// @return This function returns no value.
static void wmDamageItem(WmState *state, uint8_t *menu, uint8_t item) {
  if (item == WM_NO_WIDGET) {
    return;
  }

  GfxRect rect = state->menu.rect;
  rect.y++;

  for (uint8_t ii = 0; ii < item; ii++) {
    rect.y += wmItemHeight(wmMenuItem(menu, ii));
  }

  rect.h = wmItemHeight(wmMenuItem(menu, item));
  wmDamage(state, &rect);
}

/// @fn static void wmHighlight(WmWidgetJob *job, uint8_t item)
///
/// @brief Highlight an item of the open dropdown, repainting it and the one
/// highlighted before if that changes them.
///
/// @param job The job, whose widget is the menu bar.
/// @param item The item's index, or WM_NO_WIDGET for none.
///
/// @return This function returns no value.
static void wmHighlight(WmWidgetJob *job, uint8_t item) {
  WmState *state = job->state;
  if (state->menu.item != item) {
    uint8_t *menu = wmMenu(job->widget, state->menu.menu);
    wmDamageItem(state, menu, state->menu.item);
    state->menu.item = item;
    wmDamageItem(state, menu, item);
  }
}

/// @fn static void wmChoose(WmWidgetJob *job)
///
/// @brief Choose the open dropdown's highlighted item, if it has one:  close
/// the menu and tell the bar's owner with WIN_EVENT_COMMAND, with the bar's
/// identifier and the item's.
///
/// @param job The job, whose widget is the menu bar.
///
/// @return This function returns no value.
static void wmChoose(WmWidgetJob *job) {
  WmState *state = job->state;
  if (state->menu.item == WM_NO_WIDGET) {
    return;
  }

  const uint8_t *item = wmMenuItem(wmMenu(job->widget, state->menu.menu),
    state->menu.item);

  wmClose(state);
  wmReleaseChanged(state);
  WinEvent *event = wmSend(state, job->window->owner, WIN_EVENT_COMMAND,
    job->window->id);
  if (event != NULL) {
    event->widget = job->widget->id;
    event->value = item[0];
  }
}

/// @fn static void wmMenuPointer(WmWidgetJob *job)
///
/// @brief Carry out a pointer event on a menu bar or its open menu:  a
/// press on a title opens its menu, or closes it if it is open; with a menu
/// open, moving onto another title opens that one, the item under the
/// pointer is highlighted, a release on one chooses it, and a press
/// anywhere else closes the menu and goes no further.
///
/// @param job The job.
///
/// @return This function returns no value.
static void wmMenuPointer(WmWidgetJob *job) {
  WmState *state = job->state;
  WmMenuState *open = &state->menu;
  const GfxEvent *event = job->event;
  bool down = (event->buttons & GFX_BUTTON_LEFT) != 0;
  bool pressed = down && (!open->down);
  bool released = (!down) && open->down;

  open->down = down;
  job->handled = true;

  if ((job->widget == NULL) || (state->active != job->window->id)) {
    if (open->open) {
      wmClose(state);
    }

    return;
  }

  uint8_t title = wmTitleAt(state, job->widget, &job->rect, event->x,
    event->y);

  if (!open->open) {
    if (title != WM_NO_WIDGET) {
      wmOpen(job, title);
    }

    return;
  } else if (title != WM_NO_WIDGET) {
    if (pressed && (title == open->menu)) {
      wmClose(state);
    } else if (title != open->menu) {
      wmOpen(job, title);
    }

    return;
  }

  uint8_t item = WM_NO_WIDGET;
  uint8_t *menu = wmMenu(job->widget, open->menu);

  if (wmContains(&open->rect, event->x, event->y)) {
    item = wmItemAt(open, menu, event->y);
    if ((item != WM_NO_WIDGET) && (!wmUsable(wmMenuItem(menu, item)))) {
      item = WM_NO_WIDGET;
    }
  } else if (pressed) {
    wmClose(state);
    return;
  }

  wmHighlight(job, item);

  if (released) {
    wmChoose(job);
  }
}

/// @fn static uint8_t wmMarked(const WmWidget *bar, uint8_t letter)
///
/// @brief Find the menu whose title's marked letter is a letter, in either
/// case.
///
/// @param bar The menu bar.
/// @param letter The letter.
///
/// @return Returns the menu's index, or WM_NO_WIDGET.
static uint8_t wmMarked(const WmWidget *bar, uint8_t letter) {
  for (uint8_t ii = 0; ii < bar->numItems; ii++) {
    const uint8_t *title = wmMenu(bar, ii);
    for (uint8_t at = 1; at < *title; at++) {
      if ((title[at] == WM_MARK)
        && ((title[at + 1] | WM_LOWER_CASE) == (letter | WM_LOWER_CASE))
      ) {
        return ii;
      }
    }
  }

  return WM_NO_WIDGET;
}

/// @fn static void wmMenuKey(WmWidgetJob *job)
///
/// @brief Carry out a key on a window with a menu bar.  With no menu open,
/// F10, or an Alt pressed and released with nothing else, opens the first
/// menu, and Alt and a title's marked letter opens that menu; the Alts and
/// the release of the key that closed a menu are the bar's.  With one open,
/// every key is the menu's:  Left and Right open the menu each side, Up and
/// Down move the highlight, Enter chooses, and Escape closes it.
///
/// @param job The job, whose widget is the menu bar, or NULL if the window
///   has none.
///
/// @return This function returns no value.
static void wmMenuKey(WmWidgetJob *job) {
  WmState *state = job->state;
  WmMenuState *open = &state->menu;
  const GfxEvent *event = job->event;
  uint8_t key = event->keycode;
  bool alt = (key == GFX_KEY_LEFT_ALT) || (key == GFX_KEY_RIGHT_ALT);

  if (!open->open) {
    bool altAlone = open->altAlone;
    open->altAlone = alt && (event->down != 0);

    if ((event->down == 0) && (key == open->swallow)) {
      open->swallow = 0;
      job->handled = true;
      return;
    } else if ((job->widget == NULL) || (job->widget->numItems == 0)
      || ((job->widget->flags & WIN_WIDGET_DISABLED) != 0)
    ) {
      return;
    }

    uint8_t index = WM_NO_WIDGET;

    job->handled = alt;

    if (alt && (event->down == 0) && altAlone) {
      index = 0;
    } else if (key == GFX_KEY_F10) {
      job->handled = true;

      if (event->down != 0) {
        index = 0;
      }
    } else if ((event->down != 0) && ((event->modifiers & WM_MOD_ALT) != 0)
      && ((event->ascii | WM_LOWER_CASE) >= 'a')
      && ((event->ascii | WM_LOWER_CASE) <= 'z')
    ) {
      index = wmMarked(job->widget, event->ascii);
    }

    if (index != WM_NO_WIDGET) {
      wmOpen(job, index);
      job->handled = true;
    }

    return;
  }

  job->handled = true;

  if ((job->widget == NULL) || (open->window != job->window->id)) {
    wmClose(state);
    return;
  } else if (event->down == 0) {
    if (key == open->swallow) {
      open->swallow = 0;
    }

    return;
  }

  uint8_t *menu = wmMenu(job->widget, open->menu);
  uint8_t count = (uint8_t) job->widget->numItems;

  open->swallow = key;

  if (key == GFX_KEY_ESCAPE) {
    wmClose(state);
  } else if (key == GFX_KEY_LEFT) {
    wmOpen(job, (open->menu == 0) ? (count - 1) : (open->menu - 1));
  } else if (key == GFX_KEY_RIGHT) {
    wmOpen(job, ((open->menu + 1) == count) ? 0 : (open->menu + 1));
  } else if ((key == GFX_KEY_UP) || (key == GFX_KEY_DOWN)) {
    wmHighlight(job, wmNextUsable(menu, open->item,
      (key == GFX_KEY_UP) ? -1 : 1));
  } else if (key == GFX_KEY_ENTER) {
    wmChoose(job);
  }
}

/// @fn void* wmMenusInput(void *args)
///
/// @brief Carry out the job's event on its window's menu bar and open menu.
///
/// @param args A pointer to the WmWidgetJob, whose handled is set if the bar
///   or menu used the event.
///
/// @return Always returns the provided pointer.
void* wmMenusInput(void *args) {
  WmWidgetJob *job = (WmWidgetJob*) args;
  if (job->event->type == GFX_EVENT_KEY) {
    wmMenuKey(job);
  } else {
    wmMenuPointer(job);
  }

  return args;
}

