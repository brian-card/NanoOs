#!/usr/bin/env python3
################################################################################
#
#                       Copyright (c) 2026 Brian Card
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#                                 Brian Card
#                       https://github.com/brian-card
#
################################################################################

"""Mutation check of the window manager and its e2e tests.

Each mutation puts one defect into sysnook, or takes one feature out, and runs
through test/e2e/run.py the one test in the one simulator configuration that
should notice.  A mutation passes when that test goes red.  Sources are
restored and touched after every mutation so the e2e image cache rebuilds.

Usage:  mutate_nook.py [mutation ...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
MAIN = 'userspace/usr/src/commands/sysnook/main/main.c'
PAINT = 'userspace/usr/src/commands/sysnook/Paint/Paint.c'
START = 'userspace/usr/src/commands/sysnook/Start/Start.c'
CELLS = 'userspace/usr/src/commands/sysnook/include/WmCells.h'
CLIENT = 'userspace/usr/src/commands/sysnook/Client/Client.c'
BUSY = 'nano-os-sim/test_nook_a_second_nook_is_told_the_display_is_busy'
PAINTS = 'nano-os-sim/test_nook_paints_its_windows_exactly'
CALL = 'nano-os-sim/test_nook_cells_painted_by_call_match_cells_sent_as_records'
LANDS = 'nano-os-sim/test_nook_a_window_moved_live_lands_exactly_and_uncovers_the_desktop'
ACROSS = 'nano-os-sim/test_nook_a_window_moved_across_another_and_back_restores_it_exactly'
OFF_SCREEN = 'nano-os-sim/test_nook_a_window_moved_off_the_screen_and_back_is_repainted_exactly'
LIMITS = 'nano-os-sim/test_nook_a_moved_window_keeps_32_pixels_of_its_title_bar_on_the_screen'
RESIZE = 'nano-os-sim/test_nook_a_window_resized_live_grows_and_shrinks_exactly'
MINIMUM = 'nano-os-sim/test_nook_a_window_is_never_resized_below_its_minimum'
OUTLINE_MOVE = 'nano-os-sim/test_nook_an_outline_move_changes_nothing_but_the_outline_until_release'
OUTLINE_RESIZE = 'nano-os-sim/test_nook_an_outline_resize_changes_nothing_but_the_outline_until_release'
REVOKED = 'nano-os-sim/test_nook_exits_0_when_the_display_is_taken_back'
KILLED = 'nano-os-sim/test_nook_a_killed_client_takes_its_windows_with_it'
INPUT = 'userspace/usr/src/commands/sysnook/Input/Input.c'
DOTS = 'nano-os-sim/test_nook_a_title_too_long_for_its_window_ends_in_dots'
CLOSE = 'nano-os-sim/test_nook_close_asks_the_owner_and_only_when_released_on_it'
HOVER = 'nano-os-sim/test_nook_close_turns_red_under_the_pointer_repainting_only_itself'
CTRL_Q = 'nano-os-sim/test_nook_ctrl_q_goes_to_the_application_and_nook_keeps_running'
ALT_F4 = 'nano-os-sim/test_nook_alt_f4_asks_only_the_active_windows_owner_to_close'
POPUP = 'nano-os-sim/test_nook_a_popup_closes_on_a_press_outside_it'
ACTIVATE = 'nano-os-sim/test_nook_a_click_activates_the_window_under_it_and_tells_both'
TOPMOST = 'nano-os-sim/test_nook_a_topmost_window_stays_above_a_window_activated_after_it'
WM_H = 'userspace/usr/src/commands/sysnook/include/Wm.h'
WINDOW = 'userspace/usr/src/commands/sysnook/Window/Window.c'
MAXIMIZE = 'nano-os-sim/test_nook_maximize_fills_the_work_area_and_restore_brings_the_frame_back'
RESERVE = 'nano-os-sim/test_nook_a_reserving_window_resizes_a_maximized_one'
DOUBLE = 'nano-os-sim/test_nook_a_double_click_on_a_title_bar_maximizes'
ALT_TAB = 'nano-os-sim/test_nook_minimize_hides_a_window_and_alt_tab_shows_it_again'
DRAG_RESTORE = 'nano-os-sim/test_nook_dragging_a_maximized_window_restores_it_under_the_pointer'
OUTSIDE = 'nano-os-sim/test_nook_a_grab_zone_reaches_2_pixels_outside_the_border'
ZONES = 'nano-os-sim/test_nook_each_grab_zone_resizes_its_edges_and_holds_the_others'
LEFT_MINIMUM = 'nano-os-sim/test_nook_a_left_edge_dragged_past_the_minimum_holds_the_right_edge'
HELD = 'nano-os-sim/test_nook_a_retitle_during_an_outline_drag_is_painted_on_release'

MUTATIONS = [
 ('W1-busy-unreported', [(START,
  '    fputs("sysnook: display busy\\n", stderr);\n', '')], BUSY),
 ('W1-revocation-ignored', [(MAIN,
  '        if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {\n'
  '          return true;\n'
  '        }\n', '')], REVOKED),
 ('W2-no-border', [(PAINT,
  '    gfxCmdSetFgColor(cmd, border);\n'
  '    gfxCmdDrawRect(cmd, frame, false);\n', '')], PAINTS),
 ('W2-no-cell-edges', [(CELLS,
  '      gfxCmdDrawRect(cmd, &cell, pass == WM_PASS_FILL);',
  '      if (pass == WM_PASS_FILL) gfxCmdDrawRect(cmd, &cell, true);')],
  PAINTS),
 ('W2-preloads-never-called', [(PAINT,
  '    gfxCmdCall(cmd, state->rows[row]);\n', '')], CALL),
 ('W3-no-windows-under-damage', [(PAINT,
  '      && meets(&window->frame, &clip)\n',
  '      && meets(&window->frame, &clip)\n'
  '      && (state->paintAll || (ii == state->numWindows - 1))\n')], ACROSS),
 ('W3-off-screen-part-copied', [(MAIN,
  '    subtract(state, frame, &copied);\n', '')], OFF_SCREEN),
 ('W3-uncovered-left', [(MAIN,
  '    state->copyTarget = target;\n'
  '    subtract(state, shown, frame);\n',
  '    state->copyTarget = target;\n')], LANDS),
 ('W3-no-limits', [(MAIN,
  '    target.x = clamp(pointX - state->offset.x, WM_KEEP_TITLE - target.w,\n'
  '      width - WM_KEEP_TITLE);\n',
  '    target.x = pointX - state->offset.x;\n')], LIMITS),
 ('W4-shrink-leaves-old', [(MAIN,
  '  } else if ((shown->x == frame->x) && (shown->y == frame->y)) {\n'
  '    subtract(state, shown, frame);\n',
  '  } else if ((shown->x == frame->x) && (shown->y == frame->y)) {\n')],
  RESIZE),
 ('W4-old-border-kept', [(MAIN,
  '    addDamage(state, shown->x + shown->w - WM_BORDER, shown->y, WM_BORDER,\n'
  '      shown->h);\n', '')], RESIZE),
 ('W4-no-minimum', [(MAIN,
  '    target.w = clamp(pointX + state->offset.x - target.x, WM_MIN_WIDTH,',
  '    target.w = clamp(pointX + state->offset.x - target.x, 1,')], MINIMUM),
 ('W5-old-outline-kept', [(PAINT,
  '  if (outlineMoved && (state->outlineShown.w != 0)) {',
  '  if (false) {')], OUTLINE_MOVE),
 ('W5-outline-left-on-release', [(PAINT,
  '  if (outlineMoved && (state->outlineShown.w != 0)) {',
  '  if (outlineMoved && (state->outlineShown.w != 0)\n'
  '    && (state->outline.w != 0)) {')], OUTLINE_RESIZE),
 ('P2-no-repaint-for-an-ended-client', [(WINDOW,
  '  if (window->state != WIN_STATE_MINIMIZED) {\n'
  '    wmDamage(state, &window->frame);\n'
  '  }\n', '')], KILLED),
 ('P3-close-on-any-release', [(INPUT,
  '  if (wmButtonAt(window, pointX, pointY) != button) {\n'
  '    return;\n'
  '  } else if',
  '  if (false) {\n'
  '    return;\n'
  '  } else if')], CLOSE),
 ('P3-no-dots', [(PAINT,
  '          shown[ii] = \'.\';\n', '')], DOTS),
 ('P3-hover-repaints-the-window', [(MAIN,
  '    wmDamageButton(state, id, button);\n  }\n}',
  '    if (window != NULL) {\n'
  '      wmDamage(state, &window->frame);\n'
  '    }\n  }\n}')], HOVER),
 ('P4-ctrl-q-quits-again', [(INPUT,
  '  WinEvent *sent = wmSend(state, window->owner, WIN_EVENT_KEY,',
  '  if (event->ascii == 17) {\n    return;\n  }\n'
  '  WinEvent *sent = wmSend(state, window->owner, WIN_EVENT_KEY,')],
  CTRL_Q),
 ('P4-no-alt-f4', [(INPUT,
  '    } else if (window != NULL) {\n'
  '      wmSend(state, window->owner, WIN_EVENT_CLOSE_REQUESTED, window->id);',
  '    } else if (false) {\n'
  '      wmSend(state, window->owner, WIN_EVENT_CLOSE_REQUESTED, window->id);')],
  ALT_F4),
 ('P4-popups-stay', [(INPUT,
  '  closePopups(state, pointX, pointY);\n', '')], POPUP),
 ('P4-no-focus-events', [(WM_H,
  '    wmFocus(state, state->focus, 0);\n', '')], ACTIVATE),
 ('P4-layers-ignored-on-create', [(WINDOW,
  '  while ((index < state->numWindows)\n'
  '    && (state->windows[index].layer <= window.layer)\n'
  '  ) {',
  '  while (index < state->numWindows) {')], TOPMOST),
 ('P5-maximize-fills-the-screen', [(WINDOW,
  '    window->restored = window->frame;\n'
  '    window->frame = state->workArea;\n',
  '    window->restored = window->frame;\n'
  '    window->frame.x = 0;\n    window->frame.y = 0;\n'
  '    window->frame.w = (int16_t) state->display.width;\n'
  '    window->frame.h = (int16_t) state->display.height;\n')], MAXIMIZE),
 ('P5-maximized-not-resized', [(WINDOW,
  '    if (window->state == WIN_STATE_MAXIMIZED) {\n'
  '      GfxRect before = window->frame;',
  '    if (false) {\n'
  '      GfxRect before = window->frame;')], RESERVE),
 ('P5-slow-double-click', [(INPUT,
  '#define WM_DOUBLE_CLICK_MS 500', '#define WM_DOUBLE_CLICK_MS 1000')],
  DOUBLE),
 ('P5-no-restore-on-drag', [(INPUT,
  '    state->restoreOnDrag = (window->state == WIN_STATE_MAXIMIZED);\n',
  '    state->restoreOnDrag = false;\n')], DRAG_RESTORE),
 ('P5-alt-tab-skips-minimized', [(INPUT,
  '      && ((window->id != state->active)\n'
  '        || (window->state == WIN_STATE_MINIMIZED))',
  '      && (window->id != state->active)\n'
  '      && (window->state != WIN_STATE_MINIMIZED)')], ALT_TAB),
 ('P6-zones-only-inside', [(INPUT,
  '      area.x -= WM_ZONE_OUTSIDE;\n'
  '      area.y -= WM_ZONE_OUTSIDE;\n'
  '      area.w += 2 * WM_ZONE_OUTSIDE;\n'
  '      area.h += 2 * WM_ZONE_OUTSIDE;\n', '')], OUTSIDE),
 ('P6-no-corner-reach', [(INPUT,
  '    if (pointY < frame->y + WM_ZONE_CORNER) {\n',
  '    if (pointY < frame->y) {\n')], ZONES),
 ('P6-left-limit-moves-the-right', [(MAIN,
  '      target.x = clamp(pointX - state->offset.x, 0, right - WM_MIN_WIDTH);\n'
  '      target.w = right - target.x;\n',
  '      target.x = pointX - state->offset.x;\n'
  '      target.w = clamp(right - target.x, WM_MIN_WIDTH, width);\n')],
  LEFT_MINIMUM),
 ('P7-retitle-painted-at-once', [(PAINT,
  '  bool hold = (state->outline.w != 0) && (state->outlineShown.w != 0);',
  '  bool hold = false;')], HELD),
]

def run(test):
    env = dict(os.environ)
    env.pop('TMPDIR', None)
    try:
        process = subprocess.run(['python3', 'test/e2e/run.py', test],
            cwd=REPO, env=env, capture_output=True, text=True, timeout=1500)
    except subprocess.TimeoutExpired:
        return None
    return process.stdout

def green(out, test):
    return (out is not None) and re.search(r'^ok \d+ - ' + re.escape(test) + r'$', out, re.M) is not None

chosen = [mutation for mutation in MUTATIONS
          if len(sys.argv) < 2 or mutation[0] in sys.argv[1:]]
fails = 0
for name, edits, test in chosen:
    originals = {}
    for path, old, new in edits:
        full = os.path.join(REPO, path)
        originals.setdefault(full, open(full).read())
    try:
        for path, old, new in edits:
            full = os.path.join(REPO, path)
            text = open(full).read()
            assert text.count(old) == 1, name
            open(full, 'w').write(text.replace(old, new))
        passed = green(run(test), test)
        print(('FAIL ' if passed else 'ok   ') + name,
              ('green: ' if passed else 'red: ') + test, flush=True)
        fails += passed
    finally:
        now = time.time() + 2
        for full, text in originals.items():
            open(full, 'w').write(text); os.utime(full, (now, now))
sys.exit(1 if fails else 0)
