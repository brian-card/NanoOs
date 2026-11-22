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

Each mutation puts one defect into wm, or takes one feature out, and runs
through test/e2e/run.py the one test in the one simulator configuration that
should notice.  A mutation passes when that test goes red.  Sources are
restored and touched after every mutation so the e2e image cache rebuilds.

Usage:  mutate_wm.py [mutation ...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
MAIN = 'userspace/usr/src/commands/wm/main/main.c'
PAINT = 'userspace/usr/src/commands/wm/Paint/Paint.c'
CTRL_Q = 'nano-os-sim/test_wm_ctrl_q_gives_the_console_back_cleared_to_its_prompt'
BUSY = 'nano-os-sim/test_wm_a_second_startwm_is_told_the_display_is_busy'
PAINTS = 'nano-os-sim/test_wm_paints_its_windows_exactly'
CALL = 'nano-os-sim/test_wm_cells_painted_by_call_match_cells_sent_as_records'
LANDS = 'nano-os-sim/test_wm_a_window_moved_live_lands_exactly_and_uncovers_the_desktop'
ACROSS = 'nano-os-sim/test_wm_a_window_moved_across_another_and_back_restores_it_exactly'
OFF_SCREEN = 'nano-os-sim/test_wm_a_window_moved_off_the_screen_and_back_is_repainted_exactly'
LIMITS = 'nano-os-sim/test_wm_a_moved_window_keeps_32_pixels_of_its_title_bar_on_the_screen'
RESIZE = 'nano-os-sim/test_wm_a_window_resized_live_grows_and_shrinks_exactly'
MINIMUM = 'nano-os-sim/test_wm_a_window_is_never_resized_below_its_minimum'
OUTLINE_MOVE = 'nano-os-sim/test_wm_an_outline_move_changes_nothing_but_the_outline_until_release'
OUTLINE_RESIZE = 'nano-os-sim/test_wm_an_outline_resize_changes_nothing_but_the_outline_until_release'
REVOKED = 'nano-os-sim/test_wm_exits_0_when_the_display_is_taken_back'

MUTATIONS = [
 ('W1-quit-ignored', [(MAIN,
  '      if ((event->down != 0) && (event->ascii == WM_CTRL_Q)) {',
  '      if (false) {')], CTRL_Q),
 ('W1-busy-unreported', [(MAIN,
  '    fputs("wm: display busy\\n", stderr);\n', '')], BUSY),
 ('W1-revocation-ignored', [(MAIN,
  '    if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {\n'
  '      return true;\n'
  '    }\n', '')], REVOKED),
 ('W2-no-grip', [(PAINT,
  '  fill(cmd, WM_GRIP, frame->x + frame->w - WM_GRIP_SIZE,',
  '  fill(cmd, window->body, frame->x + frame->w - WM_GRIP_SIZE,')], PAINTS),
 ('W2-no-cell-edges', [(PAINT,
  '      gfxCmdDrawRect(cmd, &cell, pass == 0);',
  '      if (pass == 0) gfxCmdDrawRect(cmd, &cell, true);')], PAINTS),
 ('W2-preloads-never-called', [(PAINT,
  '    gfxCmdCall(cmd, state->rows[row]);\n', '')], CALL),
 ('W3-no-windows-under-damage', [(PAINT,
  '    if (intersect(&window->frame, &clip, &overlap)) {',
  '    if ((state->paintAll || (window == wmTop(state)))\n'
  '      && intersect(&window->frame, &clip, &overlap)) {')], ACROSS),
 ('W3-off-screen-part-copied', [(PAINT,
  '    subtract(state, frame, &copied);\n', '')], OFF_SCREEN),
 ('W3-uncovered-left', [(PAINT,
  '    gfxCmdCopyRect(state->cmd, &source, target);\n'
  '    subtract(state, shown, frame);\n',
  '    gfxCmdCopyRect(state->cmd, &source, target);\n')], LANDS),
 ('W3-no-limits', [(MAIN,
  '    target.x = clamp(pointX - state->offset.x, WM_KEEP_TITLE - target.w,\n'
  '      width - WM_KEEP_TITLE);\n',
  '    target.x = pointX - state->offset.x;\n')], LIMITS),
 ('W4-shrink-leaves-old', [(PAINT,
  '      shown->y + shown->h - WM_GRIP_SIZE, WM_GRIP_SIZE, WM_GRIP_SIZE };\n'
  '    subtract(state, shown, frame);\n',
  '      shown->y + shown->h - WM_GRIP_SIZE, WM_GRIP_SIZE, WM_GRIP_SIZE };\n')],
  RESIZE),
 ('W4-old-grip-kept', [(PAINT,
  '    if (intersect(&oldGrip, frame, &oldGrip)) {\n'
  '      addRect(state, &oldGrip);\n'
  '    }\n', '')], RESIZE),
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
