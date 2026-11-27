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

"""Mutation check of Graphics.h's defect fixes and their tests.

Each mutation puts one defect from the graphics series' catalog back into
src/kernel/Graphics.h, rebuilds the harness through its own makefile, and
runs each graphics_header test on its own under a timeout, since a broken
wait can leave a test blocked forever.  A mutation passes when every test it
names goes red.  The header is restored and touched after every mutation so
no later build reuses a mutated object.

Usage:  mutate_graphics_header.py [mutation ...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
HEADER = 'src/kernel/Graphics.h'
SUITE = 'test/suites/test_kernel_graphics_header.c'

MUTATIONS = [
 ('H1-split-record', '''  uint8_t *record = gfxCmdBufReserve_(buf, 1 + size);
  if (record != NULL) {
    record[0] = (uint8_t) tag;
    if (size > 0) {''', '''  uint8_t *record = gfxCmdBufReserve_(buf, 1);
  if (record != NULL) {
    record[0] = (uint8_t) tag;
    record = gfxCmdBufReserve_(buf, size) - 1;
    if (size > 0) {''',
  ['a_record_that_doesnt_fit_goes_whole_into_the_next_submit']),
 ('H2-overflow-unreported', '''      gfxCmdBufFail_(buf, GFX_ERR_CMDBUF_OVERFLOW);
      return NULL;''', '''      return NULL;''',
  ['a_record_larger_than_the_buffer_is_refused_whole',
   'a_preupload_too_long_for_the_buffer_executes_nothing']),
 ('H3-output-read-after-free', '''  if (status == GFX_OK) {
    *outImage = args->handle;
  }
  MESSAGE_ARGS_FREE(args);
  return status;
}


// =========================================================================
// FONTS''', '''  MESSAGE_ARGS_FREE(args);
  if (status == GFX_OK) {
    *outImage = args->handle;
  }
  return status;
}


// =========================================================================
// FONTS''', ['the_client_returns_what_the_driver_wrote']),
 ('H4-status-not-preset', '''  *((uint8_t*) args) = GFX_ERR_DEVICE_LOST;
''', '', ['the_driver_sees_inputs_and_a_preset_status',
          'a_driver_that_exits_before_answering_is_device_lost',
          'a_driver_killed_before_answering_is_device_lost']),
 ('H5-send-failure-ignored', '''  if (sendProcessMessageToPid(disp->driverPid, message) != processSuccess) {
    processMessageRelease(message);
    return GFX_ERR_DEVICE_LOST;
  }
  processMessageWaitForDone(message, NULL);''', '''  sendProcessMessageToPid(disp->driverPid, message);
  processMessageWaitForDone(message, NULL);''',
  ['a_driver_that_isnt_running_is_device_lost_without_a_leak']),
 ('H6-failed-one-way-leaked', '''  if (args != NULL) {
    MESSAGE_ARGS_DESTROY(args);
  }
  return status;''', '''  return status;''',
  ['a_driver_that_isnt_running_is_device_lost_without_a_leak']),
 ('H7-stack-payload', '''  MESSAGE_ARGS_ALLOCATE(GfxGetErrorArgs, args);''',
  '''  GfxGetErrorArgs argsStorage = {0};
  GfxGetErrorArgs *args = &argsStorage;''',
  ['every_message_carries_the_signature_kind_and_size',
   'a_slow_driver_still_answers']),
 ('H8-bitmap-unshared', '''  args->bitmap    = MESSAGE_BUFFER_SHARE(desc->bitmap, desc->size,
    MESSAGE_BUFFER_IN, disp->driverPid);''', '''  args->bitmap    = desc->bitmap;''',
  ['a_driver_writing_an_in_buffer_is_reported_once']),
 ('H9-submit-status-dropped', '''    if (args->status != GFX_OK) {
      gfxCmdBufFail_(buf, (GfxError) args->status);
    }''', '',
  ['a_submit_waits_in_wait_state_for_the_buffer_it_reuses']),
 ('H10-spin-wait', '''  if (message != NULL) {
    processMessageWaitForDone(message, NULL);''', '''  if (message != NULL) {
    while (processMessageDone(message) == false) processYield();''',
  ['a_submit_waits_in_wait_state_for_the_buffer_it_reuses',
   'destroy_waits_for_the_buffer_in_flight']),
 ('H11-preupload-ignores-error', '''  if (status != GFX_OK) {
    return status;
  }

  MESSAGE_ARGS_ALLOCATE(GfxPreuploadArgs, args);''', '''  MESSAGE_ARGS_ALLOCATE(GfxPreuploadArgs, args);''',
  ['a_preupload_too_long_for_the_buffer_executes_nothing']),
 ('H12-bare-type', '''  processMessageInit(message, GFX_COMMAND_SIGNATURE | type,
    args, size, /* waiting= */ true);''', '''  processMessageInit(message, type,
    args, size, /* waiting= */ true);''',
  ['every_message_carries_the_signature_kind_and_size',
   'opening_finds_the_driver_and_caches_what_it_answers']),
 ('H16-full-cache-uploads', '''  if (disp->fontCount >= GFX_MAX_FONTS) {
    return GFX_ERR_OUT_OF_HANDLES;
  }''', '', ['a_full_font_cache_refuses_an_upload']),
 ('H17-default-destroyed', '''  if (font == disp->defaultFont) {
    return GFX_ERR_INVALID_STATE;
  }''', '', ['the_default_font_cant_be_destroyed']),
 ('H18-begin-keeps-error', '''  buf->flags = flags;
  buf->error = GFX_OK;''', '''  buf->flags = flags;''',
  ['text_longer_than_the_maximum_records_nothing']),
 ('H19-cached-surface-size', '''  MESSAGE_ARGS_ALLOCATE(GfxSurfaceSizeArgs, args);
  GfxError status = gfxCall_(disp, GFX_MSG_GET_SURFACE_SIZE,
    args, sizeof(*args));''', '''  MESSAGE_ARGS_ALLOCATE(GfxSurfaceSizeArgs, args);
  GfxError status = GFX_OK;
  args->width = disp->width;
  args->height = disp->height;''',
  ['the_client_returns_what_the_driver_wrote']),
 ('H21-text-truncated', '''  if (len > GFX_TEXT_MAX) {
    gfxCmdBufFail_(buf, GFX_ERR_CMDBUF_OVERFLOW);
    return;
  }''', '', ['text_longer_than_the_maximum_records_nothing']),
 ('H22-null-source-zeroed', '''  if (srcRect == NULL) {
    gfxCmdBufFail_(buf, GFX_ERR_INVALID_STATE);
    return;
  }''', '''  GfxRect zero = { 0, 0, 0, 0 };
  if (srcRect == NULL) {
    srcRect = &zero;
  }''', ['a_missing_rectangle_records_nothing']),
 ('H24-measurement-wraps', '''  return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t) value;''',
  '''  return (uint16_t) value;''', ['measurements_of_long_strings_saturate']),
]

def tests():
    text = open(os.path.join(REPO, SUITE)).read()
    return re.findall(r'NANO_OS_KERNEL_TEST_WITH\(graphics_header, (\w+),', text)

def run(command, timeout=None):
    try: return subprocess.run(command, cwd=REPO, shell=True, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired: return None

def status(names):
    results = {}
    for test in names:
        process = run(f'./test/bin/nano-os-test graphics_header/{test}', 40)
        results[test] = (process is not None) and re.search(r'^ok \d+ - graphics_header/' + test + r'$', process.stdout, re.M) is not None
    return results

TESTS = tests()
chosen = [mutation for mutation in MUTATIONS if len(sys.argv) < 2 or mutation[0] in sys.argv[1:]]
full = os.path.join(REPO, HEADER)
orig = open(full).read()
fails = 0
for name, old, new, expect in chosen:
    assert orig.count(old) == 1, name
    assert all(test in TESTS for test in expect), name
    try:
        open(full, 'w').write(orig.replace(old, new))
        build = run('make -C test build')
        if build.returncode: print('FAIL', name, 'build'); fails += 1; continue
        results = status(TESTS); green = [test for test in expect if results[test]]
        extra = [test for test in TESTS if not results[test] and test not in expect]
        print(('FAIL ' if green else 'ok   ') + name, 'still green:' if green else 'red:', ' '.join(green or expect), ('| also red: ' + ' '.join(extra)) if extra else '', flush=True)
        fails += bool(green)
    finally:
        open(full, 'w').write(orig); now = time.time() + 2; os.utime(full, (now, now))
run('make -C test build'); results = status(TESTS)
print('restored, red:', [test for test, passed in results.items() if not passed] or 'none')
sys.exit(1 if fails else 0)
