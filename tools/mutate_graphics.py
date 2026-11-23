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

"""Mutation check of the scheduler's graphics client support and its tests.

Each mutation disables one grant, hook, or notification in Scheduler.c,
rebuilds the harness through its own makefile, and runs each
graphics_client test on its own under a timeout, since a broken
notification can leave a test waiting forever.  A mutation passes when
every test it names goes red.  Sources are restored and touched after every
mutation so no later build reuses a mutated object.

Usage:  mutate_graphics.py
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SCHED = 'src/kernel/Scheduler.c'
MUTATIONS = [
 ('no-grant', SCHED, '  processDescriptor->graphicsClient = true;\n}\n', '  processDescriptor->graphicsClient = false;\n}\n',
  ['the_client_program_is_granted_both_ways', 'an_exiting_client_is_reported_once', 'a_client_execing_itself_is_reported_and_granted_again']),
 ('no-exit-hook', SCHED, '  schedulerReleaseConsolePorts(processDescriptor);\n  schedulerEndClient(processDescriptor);\n\n  if (processDescriptor->envp != NULL) {',
  '  schedulerReleaseConsolePorts(processDescriptor);\n\n  if (processDescriptor->envp != NULL) {', ['an_exiting_client_is_reported_once']),
 ('no-remove-hook', SCHED, '  schedulerReleaseConsolePorts(processDescriptor);\n  schedulerEndClient(processDescriptor);\n  schedulerSetDirCapabilities(processDescriptor, NULL);\n',
  '  schedulerReleaseConsolePorts(processDescriptor);\n  schedulerSetDirCapabilities(processDescriptor, NULL);\n', ['a_removed_client_is_reported_once']),
 ('no-exec-hook', SCHED, '  schedulerGraphicsProgramChanged(processDescriptor, pathname);\n', '',
  ['a_client_execing_another_program_is_reported', 'a_client_execing_itself_is_reported_and_granted_again']),
 ('no-driver-revoke', SCHED, '''  removeProcessIpcCapabilityMask(&allProcesses[driverPid - 1],
    processDescriptor->processId, GFX_COMMAND_SIGNATURE,
    GFX_DRIVER_MESSAGE_MASK);
''', '', ['an_exiting_client_is_reported_once', 'a_killed_client_is_reported_once', 'a_removed_client_is_reported_once']),
 ('no-notification', SCHED,
  '    schedulerWindowServerEnded(processDescriptor->processId);\n  }\n\n  ProcessMessage *processMessage = getAvailableMessage();\n',
  '    schedulerWindowServerEnded(processDescriptor->processId);\n  }\n\n  ProcessMessage *processMessage = NULL;\n',
  ['an_exiting_client_is_reported_once', 'a_killed_client_is_reported_once', 'a_removed_client_is_reported_once']),
]
TESTS = ['the_client_program_is_granted_both_ways','another_system_command_is_not_granted','a_user_level_spawn_of_the_client_is_not_granted','an_exiting_client_is_reported_once','a_killed_client_is_reported_once','a_client_whose_stack_overflowed_is_reported_once','a_removed_client_is_reported_once','a_client_execing_another_program_is_reported','a_client_execing_itself_is_reported_and_granted_again']
def run(command, timeout=None):
    try: return subprocess.run(command, cwd=REPO, shell=True, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired: return None
def status():
    results={}
    for test in TESTS:
        process = run(f'./test/bin/nano-os-test graphics_client/{test}', 40)
        results[test] = (process is not None) and re.search(r'^ok \d+ - graphics_client/'+test, process.stdout, re.M) is not None
    return results
fails=0
for name, path, old, new, expect in MUTATIONS:
    full=os.path.join(REPO,path); orig=open(full).read()
    assert orig.count(old)==1, name
    try:
        open(full,'w').write(orig.replace(old,new))
        build=run('make -C test build')
        if build.returncode: print('FAIL',name,'build'); fails+=1; continue
        results=status(); green=[test for test in expect if results[test]]
        extra=[test for test in TESTS if not results[test] and test not in expect]
        print(('FAIL ' if green else 'ok   ')+name, 'still green:' if green else 'red:', ' '.join(green or expect), ('| also red: '+' '.join(extra)) if extra else '')
        fails+=bool(green)
    finally:
        open(full,'w').write(orig); now=time.time()+2; os.utime(full,(now,now))
run('make -C test build'); results=status()
print('restored, red:', [test for test,passed in results.items() if not passed] or 'none')
sys.exit(1 if fails else 0)
