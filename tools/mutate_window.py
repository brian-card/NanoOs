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

"""Mutation check of the scheduler's window server support and its tests.

Each mutation disables one check, grant, hook, or notification in
Scheduler.c, rebuilds the harness through its own makefile, and runs each
window_server test on its own under a timeout, since a broken notification
can leave a test waiting forever.  A mutation passes when every test it
names goes red.  Sources are restored and touched after every mutation so no
later build reuses a mutated object.

Usage:  mutate_window.py
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SCHED = 'src/kernel/Scheduler.c'
ENDED = ['an_exiting_client_is_reported_once',
         'a_killed_client_is_reported_once',
         'a_removed_client_is_reported_once',
         'a_client_execing_another_program_is_reported']
MUTATIONS = [
 ('no-graphics-client-check', SCHED,
  '  if (caller->graphicsClient == false) {\n'
  '    schedulerWindowServerArgs->errorNumber = EPERM;\n',
  '  if (false) {\n    schedulerWindowServerArgs->errorNumber = EPERM;\n',
  ['a_process_that_isnt_the_graphics_client_cant_register']),
 ('no-single-registration', SCHED,
  '  } else if ((schedulerState->windowServerPid != 0)\n'
  '    && (schedulerState->windowServerPid != caller->processId)\n'
  '  ) {',
  '  } else if (false) {',
  ['the_graphics_client_registers_and_a_second_is_busy']),
 ('no-user-check', SCHED,
  '  } else if (caller->userId != allProcesses[serverPid - 1].userId) {',
  '  } else if (false) {',
  ['another_users_process_cant_connect']),
 ('no-client-grant', SCHED,
  '    if ((addProcessIpcCapabilityMask(caller, serverPid,\n'
  '        WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK) != 0)',
  '    if ((0 != 0)',
  ['a_connecting_client_is_granted_both_ways']),
 ('no-server-grant', SCHED,
  '      || (addProcessIpcCapabilityMask(&allProcesses[serverPid - 1],\n'
  '        caller->processId, WIN_COMMAND_SIGNATURE, WIN_SERVER_MESSAGE_MASK)\n'
  '        != 0)',
  '      || (0 != 0)',
  ['a_connecting_client_is_granted_both_ways']),
 ('no-client-end-hook', SCHED,
  '  if (processDescriptor->windowClient) {\n'
  '    schedulerWindowClientEnded(processDescriptor);\n'
  '  }\n',
  '',
  ENDED),
 ('no-notification', SCHED,
  '  schedulerRevokeWindowClient(processDescriptor, serverPid);\n\n'
  '  ProcessMessage *processMessage = getAvailableMessage();',
  '  schedulerRevokeWindowClient(processDescriptor, serverPid);\n\n'
  '  ProcessMessage *processMessage = NULL;',
  ENDED),
 ('no-server-ended', SCHED,
  '    schedulerWindowServerEnded(processDescriptor->processId);\n',
  '',
  ['the_window_server_ending_revokes_every_client']),
 ('no-server-revoke', SCHED,
  '    WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK);\n'
  '  removeProcessIpcCapabilityMask(&allProcesses[serverPid - 1],\n'
  '    processDescriptor->processId, WIN_COMMAND_SIGNATURE,\n'
  '    WIN_SERVER_MESSAGE_MASK);',
  '    WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK);',
  ['a_killed_client_is_reported_once']),
]
TESTS = [
  'the_graphics_client_registers_and_a_second_is_busy',
  'a_process_that_isnt_the_graphics_client_cant_register',
  'a_connecting_client_is_granted_both_ways',
  'connecting_again_grants_nothing_new',
  'connecting_with_no_window_server_is_not_found',
  'another_users_process_cant_connect',
  'an_exiting_client_is_reported_once',
  'a_killed_client_is_reported_once',
  'a_removed_client_is_reported_once',
  'a_client_execing_another_program_is_reported',
  'a_process_that_never_connected_ends_unreported',
  'the_window_server_ending_revokes_every_client',
]


def run(command, timeout=None):
  try:
    return subprocess.run(command, cwd=REPO, shell=True, capture_output=True,
                          text=True, timeout=timeout)
  except subprocess.TimeoutExpired:
    return None


def status():
  results = {}
  for test in TESTS:
    process = run('setsid --wait timeout -s KILL 40 '
                  f'./test/bin/nano-os-test window_server/{test}', 60)
    results[test] = (process is not None) and re.search(
      r'^ok \d+ - window_server/' + test, process.stdout, re.M) is not None
  return results


fails = 0
for name, path, old, new, expect in MUTATIONS:
  full = os.path.join(REPO, path)
  original = open(full).read()
  assert original.count(old) == 1, name
  try:
    open(full, 'w').write(original.replace(old, new))
    build = run('make -C test build')
    if build.returncode:
      print('FAIL', name, 'build')
      fails += 1
      continue
    results = status()
    green = [test for test in expect if results[test]]
    extra = [test for test in TESTS
             if not results[test] and test not in expect]
    print(('FAIL ' if green else 'ok   ') + name,
          'still green:' if green else 'red:', ' '.join(green or expect),
          ('| also red: ' + ' '.join(extra)) if extra else '')
    fails += bool(green)
  finally:
    open(full, 'w').write(original)
    now = time.time() + 2
    os.utime(full, (now, now))
run('make -C test build')
results = status()
print('restored, red:',
      [test for test, passed in results.items() if not passed] or 'none')
sys.exit(1 if fails else 0)
