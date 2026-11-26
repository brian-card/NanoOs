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

"""Mutation check of the scheduler's window server and console session
support and their tests.

Each mutation disables one check, grant, hook, or notification in
Scheduler.c or Console.c, rebuilds the harness through its own makefile, and
runs each harness test on its own under a timeout, since a broken
notification can leave a test waiting forever.  A test named e2e/<name> is
the end-to-end test of that name, run in the nano-os-sim configuration on an
image the e2e runner rebuilds.  A mutation passes when every test it names
goes red.  Sources are restored and touched after every
mutation so no later build reuses a mutated object.

Usage:  mutate_window.py
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SCHED = 'src/kernel/Scheduler.c'
CONSOLE = 'src/kernel/Console.c'
BRIDGE = 'userspace/usr/src/commands/sessiontest/Bridge/Bridge.c'
ENDED = ['window_server/an_exiting_client_is_reported_once',
         'window_server/a_killed_client_is_reported_once',
         'window_server/a_removed_client_is_reported_once',
         'window_server/a_client_execing_another_program_is_reported']
MUTATIONS = [
 ('no-graphics-client-check', SCHED,
  '  if (caller->graphicsClient == false) {\n'
  '    schedulerWindowServerArgs->errorNumber = EPERM;\n',
  '  if (false) {\n    schedulerWindowServerArgs->errorNumber = EPERM;\n',
  ['window_server/a_process_that_isnt_the_graphics_client_cant_register']),
 ('no-single-registration', SCHED,
  '  } else if ((schedulerState->windowServerPid != 0)\n'
  '    && (schedulerState->windowServerPid != caller->processId)\n'
  '  ) {',
  '  } else if (false) {',
  ['window_server/the_graphics_client_registers_and_a_second_is_busy']),
 ('no-user-check', SCHED,
  '  } else if (caller->userId != allProcesses[serverPid - 1].userId) {',
  '  } else if (false) {',
  ['window_server/another_users_process_cant_connect']),
 ('no-client-grant', SCHED,
  '    if ((addProcessIpcCapabilityMask(caller, serverPid,\n'
  '        WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK) != 0)',
  '    if ((0 != 0)',
  ['window_server/a_connecting_client_is_granted_both_ways']),
 ('no-server-grant', SCHED,
  '      || (addProcessIpcCapabilityMask(&allProcesses[serverPid - 1],\n'
  '        caller->processId, WIN_COMMAND_SIGNATURE, WIN_SERVER_MESSAGE_MASK)\n'
  '        != 0)',
  '      || (0 != 0)',
  ['window_server/a_connecting_client_is_granted_both_ways']),
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
  ['window_server/the_window_server_ending_revokes_every_client']),
 ('session-login-shell-by-pid-range', SCHED,
  '    && (processDescriptor->shellPath == NULL)\n'
  '    && processDescriptor->shellSlot\n',
  '    && (processDescriptor->shellPath == NULL)\n'
  '    && (processDescriptor->processId - _schedulerState->firstShellPid\n'
  '      < _schedulerState->numShells)\n',
  ['session/a_shell_slot_outside_the_boot_range_records_its_login_shell']),
 ('session-output-by-pid-range', SCHED,
  '  if (processDescriptor->shellSlot\n'
  '    && (processDescriptor->shellOutputPid != PROCESS_ID_NOT_SET)',
  '  if ((processDescriptor->processId - _schedulerState->firstShellPid\n'
  '      < _schedulerState->numShells)\n'
  '    && (processDescriptor->shellOutputPid != PROCESS_ID_NOT_SET)',
  ['session/a_shell_slot_outside_the_boot_range_has_its_output_directed']),
 ('session-output-servicer-sends-input', SCHED,
  '  if ((addProcessIpcCapability(inputServicer, consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_INPUT_READY) != 0)',
  '  if ((addProcessIpcCapability(&allProcesses[outputPid - 1], consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_INPUT_READY) != 0)\n'
  '    || (addProcessIpcCapability(inputServicer, consolePid,\n'
  '      CONSOLE_COMMAND_SIGNATURE, CONSOLE_INPUT_READY) != 0)',
  ['session/the_output_servicer_cannot_send_a_split_port_input']),
 ('session-echo-to-input-servicer', CONSOLE,
  '    consolePort->outputPid,\n'
  '    CONSOLE_COMMAND_SIGNATURE | CONSOLE_FORWARD_STRING,',
  '    consolePort->forwardingPid,\n'
  '    CONSOLE_COMMAND_SIGNATURE | CONSOLE_FORWARD_STRING,',
  ['session/a_split_port_echoes_to_its_output_servicer_only']),
 ('session-port-index-not-reused', CONSOLE,
  '    if (consolePortInUse(&consolePorts[ii]) == false) {\n'
  '      portId = (unsigned char) ii;',
  '    if ((consolePortInUse(&consolePorts[ii]) == false)\n'
  '      && (ii >= consoleState->numConsolePorts)\n'
  '    ) {\n'
  '      portId = (unsigned char) ii;',
  ['session/a_removed_port_index_is_reused']),
 ('session-held-input-kept', CONSOLE,
  '    if (consolePort->pendingInput != NULL) {\n'
  '      processMessageSetDone(consolePort->pendingInput);',
  '    if (false) {\n'
  '      processMessageSetDone(consolePort->pendingInput);',
  ['session/removing_a_port_finishes_its_held_input']),
 ('session-anyone-removes-a-port', CONSOLE,
  '  if ((processPid(processMessageFrom(inputMessage)) == schedulerPid)\n'
  '    && (portId < CONSOLE_NUM_PORTS)',
  '  if ((portId < CONSOLE_NUM_PORTS)',
  ['session/only_the_scheduler_removes_a_port']),
 ('session-anyone-opens', SCHED,
  '  if (caller->processId != schedulerState->windowServerPid) {\n'
  '    return EPERM;\n'
  '  }\n'
  '  ProcessId display = args->display;',
  '  ProcessId display = args->display;',
  ['session/only_the_window_server_opens_a_session']),
 ('session-any-display-user', SCHED,
  '    || (displayDescriptor->userId != caller->userId)\n',
  '',
  ['session/a_display_of_another_user_is_refused']),
 ('session-level-not-set', SCHED,
  '  shell->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;\n'
  '  schedulerSetBaseHalCapabilities(shell);\n'
  '  shell->readyQueue = _schedulerState->readyQueues['
  'PRIVILEGE_LEVEL_SUPERVISOR];\n'
  '  if (shell->ipcCapabilitiesDynamic) {\n'
  '    schedFree(shell->ipcCapabilities);\n'
  '  }\n'
  '  shell->ipcCapabilities = baseSupervisorIpcCapabilities;\n'
  '  shell->numIpcCapabilities = sizeof(baseSupervisorIpcCapabilities)\n'
  '    / sizeof(baseSupervisorIpcCapabilities[0]);\n'
  '  shell->ipcCapabilitiesDynamic = false;\n'
  '  schedulerSetDirCapabilities(shell, supervisorDirCapabilities);\n',
  '  schedulerSetDirCapabilities(shell, supervisorDirCapabilities);\n',
  ['e2e/test_session_shell_and_its_commands_run_at_a_consoles_levels']),
 ('session-processes-not-killed', SCHED,
  '      && processRunning(processDescriptor)\n'
  '      && (schedulerKillSlot(processDescriptor, SIGKILL, false) == -EBUSY)\n',
  '      && processRunning(processDescriptor) && false\n'
  '      && (schedulerKillSlot(processDescriptor, SIGKILL, false) == -EBUSY)\n',
  ['e2e/test_killing_the_bridge_ends_the_session_and_its_processes']),
 ('session-port-not-removed', SCHED,
  '  schedulerRemovePort(port);\n'
  '  shell->shellSlot = SHELL_SLOT_NONE;\n',
  '  shell->shellSlot = SHELL_SLOT_NONE;\n',
  ['e2e/test_exiting_the_shell_ends_the_session_and_frees_its_port']),
 ('session-getty-instead-of-ending', SCHED,
  '  if ((processDescriptor->userId == NO_USER_ID)\n'
  '    && (processDescriptor->shellSlot == SHELL_SLOT_SESSION)\n'
  '  ) {\n'
  '    return schedulerEndSession(processDescriptor);\n'
  '  } else if',
  '  if',
  ['e2e/test_exiting_the_shell_ends_the_session_and_frees_its_port']),
 ('session-bridge-ignores-resize', BRIDGE,
  '        sendWinsize(bridge, consoleBuffer->buffer);\n',
  '        if (false) sendWinsize(bridge, consoleBuffer->buffer);\n',
  ['e2e/test_a_resize_report_through_the_bridge_sets_the_sessions_size']),
 ('session-last-slot-refused', SCHED,
  '  if ((shell == 0) || (shell > numProcesses)) {',
  '  if (shell >= numProcesses) {',
  ['session/the_last_process_slot_can_be_a_ports_shell']),
 ('no-server-revoke', SCHED,
  '    WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK);\n'
  '  removeProcessIpcCapabilityMask(&allProcesses[serverPid - 1],\n'
  '    processDescriptor->processId, WIN_COMMAND_SIGNATURE,\n'
  '    WIN_SERVER_MESSAGE_MASK);',
  '    WIN_COMMAND_SIGNATURE, WIN_CLIENT_MESSAGE_MASK);',
  ['window_server/a_killed_client_is_reported_once']),
]
TESTS = [
  'window_server/the_graphics_client_registers_and_a_second_is_busy',
  'window_server/a_process_that_isnt_the_graphics_client_cant_register',
  'window_server/a_connecting_client_is_granted_both_ways',
  'window_server/connecting_again_grants_nothing_new',
  'window_server/connecting_with_no_window_server_is_not_found',
  'window_server/another_users_process_cant_connect',
  'window_server/an_exiting_client_is_reported_once',
  'window_server/a_killed_client_is_reported_once',
  'window_server/a_removed_client_is_reported_once',
  'window_server/a_client_execing_another_program_is_reported',
  'window_server/a_process_that_never_connected_ends_unreported',
  'window_server/the_window_server_ending_revokes_every_client',
  'session/a_shell_slot_outside_the_boot_range_records_its_login_shell',
  'session/a_shell_slot_outside_the_boot_range_has_its_output_directed',
  'session/a_removed_port_index_is_reused',
  'session/removing_a_port_finishes_its_held_input',
  'session/only_the_scheduler_removes_a_port',
  'session/a_split_port_echoes_to_its_output_servicer_only',
  'session/the_output_servicer_cannot_send_a_split_port_input',
  'session/only_the_window_server_opens_a_session',
  'session/a_display_of_another_user_is_refused',
  'session/the_last_process_slot_can_be_a_ports_shell',
]


def run(command, timeout=None):
  try:
    return subprocess.run(command, cwd=REPO, shell=True, capture_output=True,
                          text=True, timeout=timeout)
  except subprocess.TimeoutExpired:
    return None


def e2eStatus(tests):
  results = {}
  for test in tests:
    name = test[len('e2e/'):]
    process = run(f'python3 test/e2e/run.py -j 1 nano-os-sim/{name}', 900)
    results[test] = (process is not None) and re.search(
      r'^ok \d+ - nano-os-sim/' + re.escape(name) + '$', process.stdout,
      re.M) is not None
  return results


def status():
  results = {}
  for test in TESTS:
    process = run('setsid --wait timeout -s KILL 40 '
                  f'./test/bin/nano-os-test {test}', 60)
    results[test] = (process is not None) and re.search(
      r'^ok \d+ - ' + re.escape(test) + '$', process.stdout, re.M) is not None
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
    results.update(e2eStatus([test for test in expect
                              if test.startswith('e2e/')]))
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
