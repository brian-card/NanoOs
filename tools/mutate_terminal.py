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


"""Mutation check of Nook's terminals and their tests:  the window server's
text grid and session input, term's parser, the key table, the console's and
scheduler's session fixes, and FakeVdp's canvas decoder.

Each mutation breaks one thing, rebuilds what it touched, and runs the tests
it names, each of which must go red.  A test named e2e/<name> is the
end-to-end test of that name in the nano-os-sim configuration, on an image
the e2e runner rebuilds; harness/<suite/name> is a kernel harness test; and
models/<Class.test> is a test in test/e2e/test_models.py.  Sources are
restored and touched after every mutation so no later build reuses a mutated
object.  Don't run it while another e2e run uses the same temporary
directory.

Usage:  mutate_terminal.py [name...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SYSNOOK = 'userspace/usr/src/commands/sysnook/'
GRID = SYSNOOK + 'Grid/Grid.c'
RESIZE = SYSNOOK + 'Resize/Resize.c'
TEMPLATE = SYSNOOK + 'Template/Template.c'
WM = SYSNOOK + 'include/Wm.h'
INPUT = SYSNOOK + 'Input/Input.c'
WINDOW = SYSNOOK + 'Window/Window.c'
MAIN = SYSNOOK + 'main/main.c'
KEYS = SYSNOOK + 'include/WmKeys.h'
PARSE = 'userspace/usr/src/commands/term/Parse/Parse.c'
CONSOLE = 'src/kernel/Console.c'
SCHED = 'src/kernel/Scheduler.c'
FAKEVDP = 'test/e2e/fakevdp.py'
PROTOCOL = 'src/kernel/GraphicsProtocol.h'
PAINT = SYSNOOK + 'Paint/Paint.c'
MUTATIONS = [
 ('decoder-off-by-a-pixel', FAKEVDP,
  '        pixels = self.pixels(left, top, columns * width, rows * height)',
  '        pixels = self.pixels(left + 1, top, columns * width, rows * height)',
  ['models/FakeVdpReadCellsTest.test_text_drawn_on_the_canvas_reads_back_'
   'exactly']),
 ('copy-when-covered', GRID,
  '  for (uint8_t ii = index + 1; ii < state->numWindows; ii++) {\n'
  '    if ((state->windows[ii].state != WIN_STATE_MINIMIZED)\n'
  '      && overlaps(&state->windows[ii].frame, band)',
  '  for (uint8_t ii = index + 1; ii < state->numWindows; ii++) {\n'
  '    if (false\n'
  '      && overlaps(&state->windows[ii].frame, band)',
  ['e2e/test_terminal_grid_scroll_of_a_covered_grid_repaints_it_exactly']),
 ('no-copy-when-uncovered', GRID,
  '      if ((!grid->noBlit) && shown && blittable(state, ii, &band)) {',
  '      if (false && blittable(state, ii, &band)) {',
  ['e2e/test_terminal_grid_scroll_of_an_uncovered_grid_copies_its_pixels']),
 ('paint-every-write', GRID,
  '    state->next = processMessageQueueWait(&soon);',
  '    state->next = NULL; (void) soon;',
  ['e2e/test_terminal_grid_burst_of_writes_is_painted_fewer_times_than_'
   'written']),
 ('no-cursor', WM,
  '  if (grid->cursorShown && (grid->cursorRow < grid->rows)',
  '  if (false && (grid->cursorRow < grid->rows)',
  ['e2e/test_terminal_grid_cursor_is_its_cell_in_swapped_colors']),
 ('no-snapping', MAIN,
  '    snap(&target.x, &target.w, across, WM_MIN_WIDTH,',
  '    (void) snap; if (false) snap(&target.x, &target.w, across,'
  ' WM_MIN_WIDTH,',
  ['e2e/test_terminal_grid_resize_snaps_to_whole_cells_and_keeps_its_'
   'cells']),
 ('resize-loses-cells', RESIZE,
  '        memcpy(&cells[to], &grid->cells[from], keepColumns);',
  '        (void) from;',
  ['e2e/test_terminal_grid_resize_snaps_to_whole_cells_and_keeps_its_'
   'cells']),
 ('wrap-a-column-early', PARSE,
  '  parser->column++;\n\n'
  '  if ((parser->column >= parser->columns)\n',
  '  parser->column++;\n\n'
  '  if (((parser->column + 1) >= parser->columns)\n',
  ['e2e/test_terminal_term_matches_the_console_for_text_and_wraps']),
 ('no-bright-colors', PARSE,
  '      bright = ANSI_COLORS_PER_SET;',
  '      bright = 0;',
  ['e2e/test_terminal_term_matches_the_console_for_colors']),
 ('scroll-a-row-too-few', PARSE,
  '      parser->rows - parser->row) + 1);',
  '      parser->rows - parser->row) + 0);',
  ['e2e/test_terminal_term_matches_the_console_for_cursor_and_erasing']),
 ('keys-to-the-owner', INPUT,
  '  if ((grid != NULL) && (grid->port != WM_NO_PORT)) {\n'
  '    uint8_t bytes[WM_KEY_MAX_BYTES];',
  '  if (false) {\n'
  '    uint8_t bytes[WM_KEY_MAX_BYTES];',
  ['e2e/test_terminal_typing_goes_to_the_session_and_never_to_term']),
 ('key-table-entry', KEYS,
  "  '2', 0, '3', 0, 0, 'H', 0, 'F', 0, 0,",
  "  '2', 0, '3', 0, 0, 'H', 0, 'G', 0, 0,",
  ['harness/unit_vdp_keyboard/the_window_server_sends_a_session_what_the_'
   'console_driver_would']),
 ('display-may-send-input', SCHED,
  '    || (addProcessIpcCapability(&allProcesses[outputPid - 1], consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_RELEASE_BUFFER) != 0)',
  '    || (addProcessIpcCapability(&allProcesses[outputPid - 1], consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_RELEASE_BUFFER) != 0)\n'
  '    || (addProcessIpcCapability(&allProcesses[outputPid - 1], consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_INPUT_READY) != 0)',
  ['e2e/test_terminal_a_display_cant_send_its_session_input_or_a_size',
   'harness/session/the_output_servicer_cannot_send_a_split_port_input']),
 ('two-session-grids', RESIZE,
  '  if ((widgets == NULL) || (size != WIN_RECORD_HEADER_SIZE)',
  '  if ((widgets == NULL) || (size < WIN_RECORD_HEADER_SIZE)',
  ['e2e/test_terminal_only_a_session_grid_gets_a_session_and_only_one']),
 ('report-of-the-screen', INPUT,
  '      (unsigned int) (grid->rows * state->display.builtInFont.glyphH),',
  '      (unsigned int) state->display.height,',
  ['e2e/test_terminal_reports_from_a_client_are_only_the_real_size']),
 ('no-winsize', RESIZE,
  '        grid->winsizeWanted = true;',
  '        grid->winsizeWanted = false;',
  ['e2e/test_terminal_termtest_size_follows_a_resize']),
 ('no-session-ended-event', GRID,
  '      WinEvent *event = wmSend(state, window->owner,\n'
  '        WIN_EVENT_SESSION_ENDED, window->id);',
  '      WinEvent *event = NULL;',
  ['e2e/test_terminal_a_display_cant_send_its_session_input_or_a_size']),
 ('no-close-on-destroy', RESIZE,
  '    windowServerCloseSession(grid->port);',
  '    (void) grid->port;',
  ['e2e/test_terminal_destroying_a_session_window_ends_its_shell']),
 ('console-waits-on-a-display', CONSOLE,
  '    consolePort->consolePrintString = consoleSessionString;',
  '    (void) consoleSessionString;',
  ['harness/session/a_display_that_stops_reading_doesnt_stop_the_console']),
 ('eight-events', PROTOCOL,
  '#define GFX_EVENTS_PER_MESSAGE 32',
  '#define GFX_EVENTS_PER_MESSAGE 8',
  ['e2e/test_terminal_keys_typed_while_the_window_server_is_busy_all_arrive']),
 ('body-filled-under-a-grid', PAINT,
  '      const GfxRect *end = &grid->margins[grid->numMargins];',
  '      fill(cmd, window->face, overlap.x, overlap.y, overlap.w, overlap.h);\n'
  '      const GfxRect *end = &grid->margins[grid->numMargins];',
  ['e2e/test_terminal_grid_row_repainted_never_shows_black_under_it']),
 ('margins-left-unfilled', PAINT,
  '      const GfxRect *end = &grid->margins[grid->numMargins];',
  '      const GfxRect *end = grid->margins;',
  ['e2e/test_terminal_grid_row_of_a_maximized_window_never_shows_black_under_'
   'it']),
]


def run(command, timeout=None):
  try:
    return subprocess.run(command, cwd=REPO, shell=True, capture_output=True,
                          text=True, timeout=timeout)
  except subprocess.TimeoutExpired:
    return None


def passes(test):
  kind, name = test.split('/', 1)
  if kind == 'e2e':
    process = run(f'python3 test/e2e/run.py -j 1 nano-os-sim/{name}', 900)
    pattern = r'^ok \d+ - nano-os-sim/' + re.escape(name) + '$'
    return (process is not None) and re.search(pattern, process.stdout,
                                               re.M) is not None
  elif kind == 'harness':
    process = run('setsid --wait timeout -s KILL 60 '
                  f'./test/bin/nano-os-test {name}', 90)
    pattern = r'^ok \d+ - ' + re.escape(name) + '$'
    return (process is not None) and re.search(pattern, process.stdout,
                                               re.M) is not None
  process = run(f'cd test/e2e && python3 test_models.py {name}', 120)
  return (process is not None) and (process.returncode == 0)


fails = 0
chosen = sys.argv[1:]
for name, path, old, new, expect in MUTATIONS:
  if chosen and name not in chosen:
    continue
  full = os.path.join(REPO, path)
  original = open(full).read()
  assert original.count(old) == 1, name
  try:
    open(full, 'w').write(original.replace(old, new))
    if any(test.startswith('harness/') for test in expect):
      build = run('make -C test build')
      if build.returncode:
        print('FAIL', name, 'build')
        fails += 1
        continue
    green = [test for test in expect if passes(test)]
    print(('FAIL ' if green else 'ok   ') + name,
          'still green:' if green else 'red:', ' '.join(green or expect),
          flush=True)
    fails += bool(green)
  finally:
    open(full, 'w').write(original)
    now = time.time() + 2
    os.utime(full, (now, now))
run('make -C test build')
sys.exit(1 if fails else 0)
