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


/// @file Family.c
///
/// @brief Activating a window that has an owner or owns others, which
/// raises its family together and gives a modal window the input of the
/// windows it blocks, Alt+Tab, and restoring a maximized window as it is
/// dragged, entered only from the Input and Window overlays.

#include "Wm.h"

/// @fn static WmWindow* wmActivateFamily(WmState *state, WmWindow *window)
///
/// @brief Activate a window the way a press on it does:  the modal window
/// that takes its input is activated instead, and the windows of its family
/// go to the top of their layer in their order, unless they are there
/// already, so that each owned window stays above its owner.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return Returns the window activated, where it now is.
static WmWindow* wmActivateFamily(WmState *state, WmWindow *window) {
  WmWindow *modal = wmModalFor(state, window);
  if (modal != NULL) {
    window = modal;
  }

  uint8_t id = window->id;
  WmWindow *root = wmRoot(state, window);
  uint8_t family[WM_MAX_WINDOWS];
  uint8_t count = 0;

  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *member = &state->windows[ii];
    if ((member == root) || wmOwns(state, root, member)) {
      family[count++] = member->id;
    }
  }

  bool raise = (root->layer == WIN_LAYER_NORMAL)
    || (root->layer == WIN_LAYER_POPUP);
  int top = state->numWindows - 1;

  while ((top >= 0) && (state->windows[top].layer != root->layer)) {
    top--;
  }

  // A family already at the top of its layer, the window last, as a dialog
  // just made is, needs no raise, which would repaint the owner over it.
  bool onTop = (family[count - 1] == id) && ((top + 1) >= count);

  for (uint8_t ii = 0; onTop && (ii < count); ii++) {
    onTop = (state->windows[top + 1 - count + ii].id == family[ii]);
  }

  raise = raise && (!onTop);

  // Activating raises the window alone, which the family's order then undoes.
  wmActivate(state, window);

  for (uint8_t ii = 0; raise && (ii < count); ii++) {
    wmRaise(state, wmFind(state, family[ii]));
  }

  return wmFind(state, id);
}

/// @fn void* wmFamilyActivate(void *args)
///
/// @brief Activate the window state->opWindow as a press on it does, and set
/// state->opWindow to the window activated, or 0 for none.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmFamilyActivate(void *args) {
  WmState *state = (WmState*) args;
  WmWindow *window = wmFind(state, state->opWindow);

  if (window != NULL) {
    window = wmActivateFamily(state, window);
  }

  state->opWindow = (window != NULL) ? window->id : 0;

  return args;
}

/// @fn void* wmFamilyAltTab(void *args)
///
/// @brief Activate the next normal window:  the one at the back, so that
/// pressing Alt+Tab again goes through them all.  A minimized one is shown
/// again, for the Input overlay.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmFamilyAltTab(void *args) {
  WmState *state = (WmState*) args;

  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    if ((window->layer == WIN_LAYER_NORMAL)
      && ((window->id != state->active)
        || (window->state == WIN_STATE_MINIMIZED))
    ) {
      if (window->state == WIN_STATE_MINIMIZED) {
        state->opWindow = window->id;
        state->opState = window->restoreState;
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window",
          "wmWindowSetState", state);
        window = wmFind(state, state->opWindow);
      }

      if (window != NULL) {
        wmActivateFamily(state, window);
      }

      return args;
    }
  }

  return args;
}

/// @fn void* wmFamilyUnmaximize(void *args)
///
/// @brief Restore a maximized window that is being moved by its title bar,
/// keeping the pointer at the same fraction of the title bar's width, so
/// that the move goes on with the restored window.  It is here, entered once
/// per such drag, to keep the Input overlay inside its window.
///
/// @param args The window manager's state, whose event's column is the
///   pointer's.
///
/// @return Always returns the provided pointer.
void* wmFamilyUnmaximize(void *args) {
  WmState *state = (WmState*) args;
  int16_t pointX = state->event->x;

  state->restoreOnDrag = false;
  WmWindow *window = wmFind(state, state->grabbed);
  if (window == NULL) {
    return args;
  }

  int16_t width = window->frame.w;

  state->opWindow = window->id;
  state->opState = WIN_STATE_NORMAL;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window", "wmWindowSetState",
    state);
  window = wmFind(state, state->grabbed);
  if ((window == NULL) || (width <= 0)) {
    return args;
  }

  state->offset.x = (int16_t) ((((int32_t) state->offset.x) * window->frame.w)
    / width);
  window->frame.x = pointX - state->offset.x;
  state->shown = window->frame;

  if (!state->liveMove) {
    state->outline = window->frame;
  }

  return args;
}
