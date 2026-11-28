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


"""Mutation check of Nook's dialogs (plan 6) and their tests.

Each mutation breaks one thing, rebuilds the e2e image, and runs the tests
it names, each of which must go red.  A test named e2e/<name> is the
end-to-end test of that name in the nano-os-sim configuration.  Sources are
restored and touched after every mutation so no later build reuses a
mutated object.  Don't run it while another e2e run uses the same temporary
directory.

Usage:  mutate_dialogs.py [name...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SYSNOOK = 'userspace/usr/src/commands/sysnook/'
INPUT = SYSNOOK + 'Input/Input.c'
FAMILY = SYSNOOK + 'Family/Family.c'
WINDOW = SYSNOOK + 'Window/Window.c'
MAIN = SYSNOOK + 'main/main.c'
DIALOGS = SYSNOOK + 'Dialogs/Dialogs.c'
MAKE = SYSNOOK + 'Make/Make.c'
FILES = SYSNOOK + 'Files/Files.c'
LISTING = SYSNOOK + 'Listing/Listing.c'
CLIENT_H = 'src/kernel/Window.h'
WM = SYSNOOK + 'include/Wm.h'
RESTACK = SYSNOOK + 'Restack/Restack.c'
FILES_H = 'src/kernel/WindowFiles.h'
E2E = 'e2e/test_dialogs_'

MUTATIONS = [
 # Step 1:  owned and modal windows.
 ('modal-lets-the-owner-take-input', INPUT,
  '    if (state->opWindow != id) {\n      return;\n    }\n\n'
  '    window = wmFind(state, id);',
  '    window = wmFind(state, id);',
  ['e2e/test_dialogs_a_press_on_a_modal_dialogs_owner_activates_the_dialog']),
 ('owned-windows-outlive-their-owner', WINDOW,
  '    if (state->windows[ii].ownerWindow == id) {',
  '    if (false) {',
  ['e2e/test_dialogs_destroying_an_owner_destroys_its_dialogs_and_their_'
   'pixels']),
 ('any-owner-accepted', WINDOW,
  '  } else if (window->owner != pid) {\n    *status = WIN_ERR_PERMISSION;',
  '  } else if (false) {\n    *status = WIN_ERR_PERMISSION;',
  ['e2e/test_dialogs_an_owner_of_another_clients_is_refused']),
 ('owner-minimized-alone', WINDOW,
  '  if ((state->numOwned == 0) || ((newState != WIN_STATE_MINIMIZED)',
  '  if ((true) || ((newState != WIN_STATE_MINIMIZED)',
  ['e2e/test_dialogs_minimizing_an_owner_hides_its_windows_and_restoring_'
   'shows_them']),
 ('owner-raised-above-its-window', FAMILY,
  '  for (uint8_t ii = 0; raise && (ii < count); ii++) {',
  '  for (uint8_t ii = 0; false && (ii < count); ii++) {',
  ['e2e/test_dialogs_minimizing_an_owner_hides_its_windows_and_restoring_'
   'shows_them']),
 # Step 2:  applications' own dialogs.
 ('dialog-ends-on-any-button', CLIENT_H,
  '    } else if ((event.type == WIN_EVENT_COMMAND)\n'
  '      && ((event.detail & (WIN_WIDGET_ENDS_DIALOG | WIN_WIDGET_DEFAULT\n'
  '        | WIN_WIDGET_CANCEL)) != 0)\n    ) {',
  '    } else if (event.type == WIN_EVENT_COMMAND) {',
  [E2E + 'an_applications_dialog_ends_only_on_its_ending_buttons']),
 # Step 3:  message dialogs.
 ('escape-presses-nothing', MAKE,
  '      flags |= WIN_WIDGET_CANCEL;',
  '      (void) 0;',
  [E2E + 'each_icon_and_button_set_is_drawn_and_answers_exactly']),
 ('wrap-mid-word', MAKE,
  '      end = space;\n      next = space;',
  '      (void) space;',
  [E2E + 'a_message_wraps_at_the_word_that_doesnt_fit']),
 ('icon-unfilled', MAKE,
  '    params[sizeof(circle)] = 1;',
  '    params[sizeof(circle)] = 0;',
  [E2E + 'a_message_is_drawn_exactly_and_answers_with_its_button']),
 # Step 4:  input dialogs.
 ('input-text-not-selected', DIALOGS,
  '    focus->selected = (open->kind == WIN_DIALOG_INPUT)',
  '    focus->selected = false && (open->kind == WIN_DIALOG_INPUT)',
  [E2E + 'an_input_dialog_gives_back_exactly_what_was_typed']),
 # Step 5:  file dialogs.
 ('dotdot-always-sent', FILES_H,
  '  if ((*name != \'\\0\') && winListable_(set, path, parentLength)) {',
  '  if (*name != \'\\0\') {',
  [E2E + 'dotdot_is_listed_only_where_the_parent_may_be_listed',
   E2E + 'a_users_places_are_its_capabilities_and_home_has_no_dotdot']),
 ('places-ignore-create', FILES_H,
  '        needed |= DIRCAP_CREATE;',
  '        (void) 0;',
  [E2E + 'a_users_places_are_its_capabilities_and_home_has_no_dotdot']),
 ('listing-error-ignored', LISTING,
  '  } else if (listing->errorNumber == 0) {\n    wmShowDirectory(job, path);',
  '  } else if (true) {\n    wmShowDirectory(job, path);',
  [E2E + 'a_directory_the_application_may_not_list_shows_why']),
 ('replaced-without-asking', FILES,
  '  } else if (open) {\n    wmFinish(job, WIN_RESULT_OK);\n  } else {',
  '  } else if (true) {\n    wmFinish(job, WIN_RESULT_OK);\n  } else {',
  [E2E + 'saving_over_a_file_asks_first']),
 ('suffix-ignored', LISTING,
  '  if ((entry[0] == WIN_ENTRY_DIRECTORY) || (dialog->suffix == NULL)) {',
  '  if (true) {',
  [E2E + 'a_suffix_shows_only_its_files_and_none_shows_every_file']),
 ('no-dots-after-a-cut', LISTING,
  '  if ((listed < shown) || dialog->truncated) {',
  '  if (false) {',
  [E2E + 'a_long_directory_ends_with_dots_after_what_fits']),
 ('dialog-damage-left-unpainted', MAIN,
  '        if (frameNeeded(state)) {\n          paint(state);\n        }',
  '',
  [E2E + 'an_open_dialog_lists_directories_first_and_moves_between']),
 # A dialog opening is drawn once, with nothing of its owner over it.
 ('family-raised-when-on-top', FAMILY,
  '  raise = raise && (!onTop);',
  '  (void) onTop;',
  [E2E + 'a_dialog_opening_is_drawn_once_with_nothing_over_it']),
 ('decorations-repaint-what-is-under-them', WM,
  '    wmDamageOwned(state, &sides[ii], id);',
  '    wmDamage(state, &sides[ii]);',
  [E2E + 'a_dialog_opening_is_drawn_once_with_nothing_over_it']),
 ('covered-damage-kept', RESTACK,
  '  wmDropCovered(state);\n',
  '',
  [E2E + 'a_dialog_opening_is_drawn_once_with_nothing_over_it']),
 # Step 6:  events kept while a dialog call waits.
 ('other-windows-events-dropped', CLIENT_H,
  '    session->kept[session->numKept++] = *event;',
  '    (void) 0;',
  [E2E + 'events_for_other_windows_wait_for_a_message_to_end']),
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


def main(chosen):
  fails = 0
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
  return 1 if fails else 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
