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

"""Mutation check of vdp.drv's Graphics overlay fixes and their e2e tests.

Each mutation puts one driver defect from the graphics series' catalogue
back into the Graphics overlay and runs, through test/e2e/run.py, the one
test in the one simulator configuration that should catch it.  A mutation
passes when that test goes red.  The source is restored and touched after
every mutation so the e2e image cache rebuilds.

Usage:  mutate_vdp_graphics.py [mutation ...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SOURCE = 'userspace/usr/src/lib/vdp.drv/Graphics/Graphics.c'
EVERY = 'test_graphics_driver_answers_every_client_message'
FONTS = 'test_graphics_font_slots_are_reused_and_destroyed_fonts_refused'

MUTATIONS = [
 ('D1-unanswered', '''  } else if ((payload != NULL) && (processMessageSize(message) > 0)) {
    *((uint8_t*) payload) = status;
  }''', '''  }''', 'nano-os-sim/' + EVERY),
 ('D2-freed-not-destroyed', '''      MESSAGE_ARGS_DESTROY(payload);''', '''      free(payload);''',
  'nano-os-sim-checked/' + EVERY),
 ('D3-slots-never-reused', '''  vdpOutput->fontsUsed &= (uint8_t) ~(1 << slot);
''', '', 'nano-os-sim/' + FONTS),
 ('D4-freed-handle-accepted', '''    || ((vdpOutput->fontsUsed & (1 << slot)) == 0)
''', '', 'nano-os-sim/' + FONTS),
 ('D6-size-unchecked', '''  if ((processMessageSize(message) != _vdpArgsSizes[type])
    || ((_vdpArgsSizes[type] > 0) && (payload == NULL))
  ) {''', '''  if ((_vdpArgsSizes[type] > 0) && (payload == NULL)) {''',
  'nano-os-sim/' + EVERY),
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
full = os.path.join(REPO, SOURCE)
orig = open(full).read()
fails = 0
for name, old, new, test in chosen:
    assert orig.count(old) == 1, name
    try:
        open(full, 'w').write(orig.replace(old, new))
        passed = green(run(test), test)
        print(('FAIL ' if passed else 'ok   ') + name, ('still green: ' if passed else 'red: ') + test, flush=True)
        fails += passed
    finally:
        open(full, 'w').write(orig); now = time.time() + 2; os.utime(full, (now, now))
sys.exit(1 if fails else 0)
