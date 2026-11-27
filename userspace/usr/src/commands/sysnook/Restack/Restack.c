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


/// @file Restack.c
///
/// @brief What a change of the active or the top window repaints, and where
/// a window whose contents changed shows, entered from the Paint overlay
/// only for a frame that needs one or the other.

#include "Wm.h"

/// @fn static void wmDamageFocused(WmState *state, uint8_t id)
///
/// @brief Repaint a window's focused widget, whose focus outline and caret
/// show only while its window is active.
///
/// @param state The window manager's state.
/// @param id The window's identifier, or 0.
///
/// @return This function returns no value.
static void wmDamageFocused(WmState *state, uint8_t id) {
  WmWindow *window = wmFind(state, id);
  WmWidgets *widgets = (window != NULL) ? (WmWidgets*) window->widgets
    : NULL;
  if ((widgets != NULL) && (widgets->focus < widgets->count)) {
    wmDamageWidget(state, window, &widgets->widget[widgets->focus]);
  }
}

/// @def WM_MAX_PIECES
///
/// @brief The most pieces paintOwned splits a rectangle into before it
/// repaints the whole rectangle instead.
#define WM_MAX_PIECES 8

/// @fn static uint8_t subtract(GfxRect *pieces, uint8_t count,
///   const GfxRect *cover)
///
/// @brief Take a rectangle out of a set of pieces, splitting each piece it
/// meets into the bands around it.
///
/// @param pieces The pieces, room for WM_MAX_PIECES of them.  A piece taken out
///   whole is left with no width.
/// @param count The number of pieces.
/// @param cover The rectangle to take out.
///
/// @return Returns the new number of pieces, or WM_MAX_PIECES + 1 if they would
/// be too many.
static uint8_t subtract(GfxRect *pieces, uint8_t count, const GfxRect *cover) {
  uint8_t kept = count;
  for (uint8_t ii = 0; ii < kept; ii++) {
    GfxRect piece = pieces[ii];
    GfxRect overlap;
    if ((piece.w == 0) || !wmIntersect(&piece, cover, &overlap)) {
      continue;
    }
    int16_t pieceBottom = piece.y + piece.h;
    int16_t overlapBottom = overlap.y + overlap.h;
    GfxRect bands[4] = {
      { piece.x, piece.y, piece.w, overlap.y - piece.y },
      { piece.x, overlapBottom, piece.w, pieceBottom - overlapBottom },
      { piece.x, overlap.y, overlap.x - piece.x, overlap.h },
      { overlap.x + overlap.w, overlap.y,
        piece.x + piece.w - overlap.x - overlap.w, overlap.h },
    };
    pieces[ii].w = 0;
    for (uint8_t jj = 0; jj < 4; jj++) {
      if ((bands[jj].w <= 0) || (bands[jj].h <= 0)) {
        continue;
      } else if (count == WM_MAX_PIECES) {
        return WM_MAX_PIECES + 1;
      }
      pieces[count++] = bands[jj];
    }
  }
  return count;
}

/// @fn void* wmOcclude(void *args)
///
/// @brief Cut each rectangle of damage where a window's own contents changed
/// down to where that window shows:  the pieces of it, inside the window,
/// that no window over it and no open menu covers.  A rectangle that would
/// split into more pieces than there is room for is repainted whole
/// instead.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmOcclude(void *args) {
  WmState *state = (WmState*) args;
  GfxRect damage[WM_MAX_DAMAGE];
  uint8_t owners[WM_MAX_DAMAGE];
  uint8_t count = 0;
  for (uint8_t ii = 0; ii < state->numDamage; ii++) {
    const GfxRect *rect = &state->damage[ii];
    WmWindow *window = wmFind(state, state->damageOwner[ii]);
    GfxRect pieces[WM_MAX_PIECES];
    uint8_t numPieces = 1;
    if ((window == NULL) || (window->state == WIN_STATE_MINIMIZED)) {
      numPieces = WM_MAX_PIECES + 1;
    } else if (!wmIntersect(rect, &window->frame, &pieces[0])) {
      continue;
    }
    for (WmWindow *above = window + 1; (numPieces <= WM_MAX_PIECES)
      && (above < &state->windows[state->numWindows]); above++
    ) {
      if (above->state != WIN_STATE_MINIMIZED) {
        numPieces = subtract(pieces, numPieces, &above->frame);
      }
    }
    if ((numPieces <= WM_MAX_PIECES) && state->menu.open) {
      numPieces = subtract(pieces, numPieces, &state->menu.rect);
    }
    uint8_t kept = 0;
    for (uint8_t jj = 0;
      (numPieces <= WM_MAX_PIECES) && (jj < numPieces);
      jj++
    ) {
      kept += (pieces[jj].w > 0) ? 1 : 0;
    }
    if ((numPieces > WM_MAX_PIECES)
      || (count + kept + (state->numDamage - ii - 1) > WM_MAX_DAMAGE)
    ) {
      damage[count] = *rect;
      owners[count++] = 0;
      continue;
    }
    for (uint8_t jj = 0; jj < numPieces; jj++) {
      if (pieces[jj].w > 0) {
        damage[count] = pieces[jj];
        owners[count++] = window->id;
      }
    }
  }
  memcpy(state->damage, damage, count * sizeof(GfxRect));
  memcpy(state->damageOwner, owners, count);
  state->numDamage = count;
  return args;
}

/// @fn void* wmRestack(void *args)
///
/// @brief Repaint what the windows a raised window moved above were
/// covering of it, and the focused widgets of the windows that stopped and
/// started being active.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmRestack(void *args) {
  WmState *state = (WmState*) args;
  WmWindow *raised = wmFind(state, state->raised);
  if (raised != NULL) {
    for (uint8_t ii = state->raisedFrom;
      &state->windows[ii] < raised; ii++
    ) {
      const WmWindow *above = &state->windows[ii];
      GfxRect covered;
      if ((above->state != WIN_STATE_MINIMIZED)
        && wmIntersect(&raised->frame, &above->frame, &covered)
      ) {
        wmDamage(state, &covered);
      }
    }
  }
  state->raised = 0;
  for (uint8_t ii = 0; ii < 2; ii++) {
    wmDamageFocused(state, state->refocus[ii]);
    state->refocus[ii] = 0;
  }
  return args;
}
