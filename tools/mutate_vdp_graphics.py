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

"""Mutation check of vdp.drv's graphics fixes and their e2e tests.

Each mutation puts one driver defect from the graphics series' catalogue
back, or takes one feature out, and runs through test/e2e/run.py the one
test in the one simulator configuration that should notice.  A mutation
passes when that test goes red, or, for one marked GREEN, when it stays
green because another path covers for what was taken out.  Sources are
restored and touched after every mutation so the e2e image cache rebuilds.

Usage:  mutate_vdp_graphics.py [mutation ...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DRIVER = 'userspace/usr/src/lib/vdp.drv/'
GRAPHICS = DRIVER + 'Graphics/Graphics.c'
MAIN = DRIVER + 'main/main.c'
SETUP = DRIVER + 'Startup/ScreenSetup.c'
EVERY = 'nano-os-sim/test_graphics_driver_answers_every_client_message'
FONTS = 'nano-os-sim/test_graphics_font_slots_are_reused_and_destroyed_fonts_refused'
BUSY = 'nano-os-sim/test_graphics_a_second_client_is_busy_and_refused'
CLOSE = 'nano-os-sim/test_graphics_closing_the_display_frees_it'
EXIT = 'nano-os-sim/test_graphics_an_owner_that_exits_frees_the_display'
EXITED = 'nano-os-sim/test_graphics_the_display_is_free_as_soon_as_its_owner_has_exited'
OWNER_FONTS = 'nano-os-sim/test_graphics_an_owners_fonts_are_freed_when_it_exits'
HELD = 'nano-os-sim/test_graphics_console_text_is_held_while_owned_and_replayed'
KEYS = 'nano-os-sim/test_graphics_keys_typed_while_owned_are_dropped'
BURST = 'nano-os-sim/test_graphics_a_long_burst_while_owned_replays_whole_lines'
RESTART = 'nano-os-sim/test_graphics_a_driver_restart_frees_the_display'
RED, GREEN = 'red', 'green'

CLIENT_ENDED_IGNORED = (GRAPHICS, '''        == vdpOutput->owner)
    ) {''', '''        == PROCESS_ID_NOT_SET)
    ) {''')
NO_FALLBACK = (GRAPHICS, '''    && ((processRunning(vdpOutput->ownerDescriptor) == false)
      || (vdpOutput->ownerDescriptor->graphicsClient == false))''',
  '''    && false''')

MUTATIONS = [
 ('D1-unanswered', [(GRAPHICS, '''  } else if ((payload != NULL) && (processMessageSize(message) > 0)) {
    *((uint8_t*) payload) = status;
  }''', '''  }''')], EVERY, RED),
 ('D2-freed-not-destroyed', [(GRAPHICS, '''      MESSAGE_ARGS_DESTROY(payload);''',
  '''      free(payload);''')], EVERY.replace('nano-os-sim/', 'nano-os-sim-checked/'), RED),
 ('D3-slots-never-reused', [(GRAPHICS, '''  vdpOutput->fontsUsed &= (uint8_t) ~(1 << slot);
''', '')], FONTS, RED),
 ('D4-freed-handle-accepted', [(GRAPHICS, '''    || ((vdpOutput->fontsUsed & (1 << slot)) == 0)
''', '')], FONTS, RED),
 ('D6-size-unchecked', [(GRAPHICS, '''  if ((processMessageSize(message) != _vdpArgsSizes[type])
    || ((_vdpArgsSizes[type] > 0) && (payload == NULL))
  ) {''', '''  if ((_vdpArgsSizes[type] > 0) && (payload == NULL)) {''')], EVERY, RED),
 ('G1-busy-unchecked', [(GRAPHICS, '''  } else if (vdpOutput->owner != processPid(sender)) {
    return GFX_ERR_DISPLAY_BUSY;
  }''', '''  }''')], BUSY, RED),
 ('G1-owner-unchecked', [(GRAPHICS, '''  } else if (vdpOutput->owner != processPid(sender)) {
    status = GFX_ERR_NOT_OWNER;
  } else if (type == GFX_MSG_CLOSE_DISPLAY) {''',
  '''  } else if (type == GFX_MSG_CLOSE_DISPLAY) {''')], BUSY, RED),
 ('G5-logical-coordinates', [(GRAPHICS, '''  23, 0, 0xC0, 0,
''', '')], BUSY, RED),
 ('G3-close-keeps-ownership', [(GRAPHICS, '''  } else if (type == GFX_MSG_CLOSE_DISPLAY) {
    vdpRelease(vdpOutput, false);''', '''  } else if (type == GFX_MSG_CLOSE_DISPLAY) {''')], CLOSE, RED),
 ('G3-client-ended-ignored', [CLIENT_ENDED_IGNORED], EXIT, RED),
 ('G3-fallback-covers-client-ended', [CLIENT_ENDED_IGNORED], EXITED, GREEN),
 ('G3-neither-release', [CLIENT_ENDED_IGNORED, NO_FALLBACK], EXITED, RED),
 ('G3-fonts-kept', [(GRAPHICS, '''    vdpDestroyFont(vdpOutput, (GfxHandle) (VDP_FONT_ID_BASE + slot));''',
  '''    (void) slot;''')], OWNER_FONTS, RED),
 ('G2-text-drawn-while-owned', [(MAIN, '''  if (vdpOutput->heldText != NULL) {
    vdpBufferOutput(vdpOutput->heldText, (const uint8_t*) text, strlen(text));
    return;
  }
''', '')], HELD, RED),
 ('G2-keys-reach-the-shell', [(MAIN, '''        }
        continue;
      }''', '''        }
      }''')], KEYS, RED),
 ('G2-no-hot-key', [(MAIN, '''        if (vdpIsReleaseKey(input->parser.data)) {''',
  '''        if (false) {''')], HELD, RED),
 ('G2-owner-not-told', [(GRAPHICS, '''    vdpRelease(vdpOutput, true);''',
  '''    vdpRelease(vdpOutput, false);''')], HELD, RED),
 ('G2-replay-untrimmed', [(GRAPHICS, '''  if (heldText->length == VDP_OUTPUT_BUFFER_SIZE) {''',
  '''  if (false) {''')], BURST, RED),
 ('G3-restart-keeps-context', [(SETUP, '''  23, 0, 0xC8, 0, 0,
  23, 0, 0xC8, 1, 1,
''', '')], RESTART, RED),
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
for name, edits, test, expect in chosen:
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
        caught = (passed == (expect == GREEN))
        print(('ok   ' if caught else 'FAIL ') + name, ('green: ' if passed else 'red: ') + test
              + ('' if expect == RED else ' (expected green)'), flush=True)
        fails += not caught
    finally:
        now = time.time() + 2
        for full, text in originals.items():
            open(full, 'w').write(text); os.utime(full, (now, now))
sys.exit(1 if fails else 0)
