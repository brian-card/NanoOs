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
DRAW = DRIVER + 'Draw/Draw.c'
IMAGES = DRIVER + 'Images/Images.c'
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
CLEARS = 'nano-os-sim/test_graphics_clears_fill_exactly_the_viewport_and_scissor'
NESTED = 'nano-os-sim/test_graphics_nested_scissors_intersect_and_a_fifth_push_is_an_error'
BAD = 'nano-os-sim/test_graphics_a_bad_record_stops_the_buffer'
ONCE = 'nano-os-sim/test_graphics_an_unchanged_clip_is_sent_once'
PRIMITIVES = 'nano-os-sim/test_graphics_primitives_draw_with_exact_edges'
TEXT = 'nano-os-sim/test_graphics_text_draws_control_bytes_as_glyphs'
PICTURES = 'nano-os-sim/test_graphics_images_draw_with_transparent_pixels_skipped'
SUB_IMAGE = 'nano-os-sim/test_graphics_a_sub_image_draws_only_its_part'
LARGE = 'nano-os-sim/test_graphics_a_large_image_uploads_in_blocks'
COPIES = 'nano-os-sim/test_graphics_overlapping_copies_move_exact_pixels'
CUT = 'nano-os-sim/test_graphics_copies_are_clipped_and_cut_to_the_screen'
SCROLL = 'nano-os-sim/test_graphics_scrolling_clears_the_uncovered_strip'
PRELOAD = 'nano-os-sim/test_graphics_a_preload_draws_at_each_origin_it_is_called_at'
PRELOAD_REFUSED = 'nano-os-sim/test_graphics_a_preload_may_not_change_the_clip_or_origin'
FINISH = 'nano-os-sim/test_graphics_finish_waits_for_the_vdp'
CAPTURE = 'nano-os-sim/test_graphics_a_capture_draws_back_the_same_pixels'
FREED = 'nano-os-sim/test_graphics_an_owners_images_and_preloads_are_freed_when_it_exits'
REVOKED_FINISH = 'nano-os-sim/test_graphics_revoking_answers_a_waiting_finish'
KILLED_FINISH = 'nano-os-sim/test_graphics_killing_an_owner_waiting_on_finish_frees_the_display'
SET_UP = 'nano-os-sim/test_graphics_the_display_is_busy_until_the_driver_has_set_it_up'
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
 ('G4-origin-ignored', [(DRAW, '''  int16_t firstX = words[0] + walk->origin.x;
  int16_t firstY = words[1] + walk->origin.y;''', '''  int16_t firstX = words[0];
  int16_t firstY = words[1];''')], PRIMITIVES, RED),
 ('G4-viewport-top-first', [(DRAW, '''    24, VDP_WORD(rect->x), VDP_WORD(rect->y + rect->h - 1),
    VDP_WORD(rect->x + rect->w - 1), VDP_WORD(rect->y)''', '''    24, VDP_WORD(rect->x), VDP_WORD(rect->y),
    VDP_WORD(rect->x + rect->w - 1), VDP_WORD(rect->y + rect->h - 1)''')],
  CLEARS, RED),
 ('G4-clip-always-sent', [(DRAW, '''  if (memcmp(clip, &draw->vdpClip, sizeof(*clip)) != 0) {''',
  '''  if (true) {''')], ONCE, RED),
 ('G4-blend-unmapped', [(DRAW, '''{ 0, 2, 1, 3, 4 }''', '''{ 0, 1, 2, 3, 4 }''')],
  PRIMITIVES, RED),
 ('G4-text-unescaped', [(DRAW, '''    if ((text[ii] < 32) || (text[ii] == 127)) {''',
  '''    if (false) {''')], TEXT, RED),
 ('G4-copy-top-left', [(DRAW, '''  vdpPlot(walk, VDP_PLOT_COPY, toLeft, toTop + height - 1);''',
  '''  vdpPlot(walk, VDP_PLOT_COPY, toLeft, toTop);''')], COPIES, RED),
 ('G4-copy-source-uncut', [(DRAW, '''  if (walk->preload == false) {
    if (left < 0) {''', '''  if (false) {
    if (left < 0) {''')], CUT, RED),
 ('G4-scroll-strip-kept', [(DRAW, '''  if (deltaY > 0) {
    vdpFillBackground(walk, left, top, right, top + deltaY - 1);''', '''  if (false) {
    vdpFillBackground(walk, left, top, right, top + deltaY - 1);''')], SCROLL, RED),
 ('G4-call-keeps-pen', [(DRAW, '''    vdpEmit(&walk->sink, call, sizeof(call));
    vdpForgetPen(walk->pen);''', '''    vdpEmit(&walk->sink, call, sizeof(call));''')],
  PRELOAD, RED),
 ('G4-preload-scissor-allowed', [(DRAW, '''    if (walk->preload == true) {
      return GFX_ERR_INVALID_STATE;
    }
    vdpScissor(walk, tag, words);''', '''    vdpScissor(walk, tag, words);''')],
  PRELOAD_REFUSED, RED),
 ('G4-scissor-depth-unchecked', [(DRAW, '''    if (draw->clipDepth == GFX_MAX_SCISSOR_DEPTH) {''',
  '''    if (draw->clipDepth == GFX_MAX_SCISSOR_DEPTH + 1) {''')], NESTED, RED),
 ('G4-short-record-accepted', [(DRAW, '''    if (length > left) {
      return GFX_ERR_INVALID_STATE;
    }''', '')], BAD, RED),
 ('G4-sub-image-clip-forgotten', [(DRAW, '''  vdpEmit(&walk->sink, viewport, sizeof(viewport));
  walk->draw->vdpClip = *rect;''', '''  vdpEmit(&walk->sink, viewport, sizeof(viewport));''')],
  SUB_IMAGE, RED),
 ('G4-rgba2222-unconverted', [(IMAGES, '''    uint8_t value = *pixel;
    return''', '''    uint8_t value = *pixel;
    return value;
    return''')], PICTURES, RED),
 ('G4-blocks-unsplit', [(IMAGES, '''  uint32_t pixelsPerBlock = 0xFFFF / vdpBytesPerPixel;''',
  '''  uint32_t pixelsPerBlock = 0xFFFFFF;''')], LARGE, RED),
 ('G4-capture-past-corner', [(IMAGES, '''    25, 4, VDP_WORD(right - 1), VDP_WORD(bottom - 1),''',
  '''    25, 4, VDP_WORD(right), VDP_WORD(bottom),''')], CAPTURE, RED),
 ('G4-finish-not-held', [(GRAPHICS, '''  vdpOutput->finishMessage = message;
  vdpOutput->graphicsMessage = NULL;''', '''  (void) message;''')], FINISH, RED),
 ('G4-finish-same-cookie', [(GRAPHICS, '''  uint8_t cookie = (uint8_t) (vdpOutput->input->pollCookie + 1);''',
  '''  uint8_t cookie = VDP_FINISH_POLL_COOKIE;''')], REVOKED_FINISH, RED),
 ('G4-images-kept', [(GRAPHICS, '''  vdpFreeImages,
''', '')], FREED, RED),
 ('G4-preloads-kept', [(GRAPHICS, '''  vdpFreePreloads,
''', '')], FREED, RED),
 ('G4-open-before-setup', [(GRAPHICS, '''  if (vdpOutput->listening == false) {
    return GFX_ERR_DISPLAY_BUSY;
  } else if (vdpOutput->owner == PROCESS_ID_NOT_SET) {''', '''  if (vdpOutput->owner == PROCESS_ID_NOT_SET) {''')], SET_UP, RED),
 ('G4-killed-finish-answered', [(GRAPHICS, '''(revoke == true) ? GFX_ERR_NOT_OWNER : VDP_UNKNOWN''',
  '''GFX_ERR_NOT_OWNER''')], KILLED_FINISH, RED),
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
