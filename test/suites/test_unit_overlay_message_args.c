///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_overlay_message_args.c
///
/// @brief             Tests of the MESSAGE_ARGS_ALLOCATE, MESSAGE_ARGS_FREE,
///                    MESSAGE_BUFFER_SHARE, and MESSAGE_BUFFER_UNSHARE forms
///                    that overlays get from ExecutiveProcesses.h, against a
///                    fake executive API in each configuration.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

// The kernel's overlayMap is a pointer; an overlay's is the map itself.
#define overlayMap testOverlayMap
#include "../../userspace/usr/include/ExecutiveProcesses.h"

NanoOsOverlayMap testOverlayMap;

typedef struct FakeCalls {
  int allocates;
  size_t allocateSize;
  int frees;
  void *freed;
  int shares;
  void *sharePtr;
  size_t shareSize;
  uint8_t shareDirection;
  uint8_t shareRecipient;
  int unshares;
  void *unshareShared;
  void *unshareOriginal;
  uint8_t unshareDirection;
} FakeCalls;

static FakeCalls calls;
static uint8_t fakeStorage[64];
static uint8_t fakeShared[16];
static NanoOsExecutiveApi fakeExecutiveApi;
static NanoOsUserApi fakeUserApi;
static NanoOsApi fakeApi = { .userApi = &fakeUserApi };

/// @fn static void* fakeAllocate(size_t size)
///
/// @brief Record an allocation and hand back fakeStorage.
///
/// @param size The number of bytes requested.
///
/// @return Returns fakeStorage.
static void* fakeAllocate(size_t size) {
  calls.allocates++;
  calls.allocateSize = size;
  return fakeStorage;
}

/// @fn static void fakeFree(void *ptr)
///
/// @brief Record a free.
///
/// @param ptr The storage freed.
///
/// @return This function returns no value.
static void fakeFree(void *ptr) {
  calls.frees++;
  calls.freed = ptr;
}

/// @fn static void* fakeShare(void *ptr, size_t size, uint8_t direction,
///   uint8_t recipient)
///
/// @brief Record a share and hand back fakeShared.
///
/// @param ptr The buffer.
/// @param size The buffer's size.
/// @param direction The buffer's direction.
/// @param recipient The process the buffer is shared with.
///
/// @return Returns fakeShared.
static void* fakeShare(void *ptr, size_t size, uint8_t direction,
  uint8_t recipient
) {
  calls.shares++;
  calls.sharePtr = ptr;
  calls.shareSize = size;
  calls.shareDirection = direction;
  calls.shareRecipient = recipient;
  return fakeShared;
}

/// @fn static void fakeUnshare(void *shared, void *original,
///   uint8_t direction)
///
/// @brief Record an unshare.
///
/// @param shared What the share returned.
/// @param original The buffer that was shared.
/// @param direction The buffer's direction.
///
/// @return This function returns no value.
static void fakeUnshare(void *shared, void *original, uint8_t direction) {
  calls.unshares++;
  calls.unshareShared = shared;
  calls.unshareOriginal = original;
  calls.unshareDirection = direction;
}

/// @fn static void useFakeApi(bool checked)
///
/// @brief Point the test's overlay map at a fake API in the given
/// configuration, as an executive process sees it, and clear the recorded
/// calls.
///
/// @param checked Whether the fake reports the checked configuration.
///
/// @return This function returns no value.
static void useFakeApi(bool checked) {
  memset(&calls, 0, sizeof(calls));
  memset(&fakeExecutiveApi, 0, sizeof(fakeExecutiveApi));
  memset(&fakeUserApi, 0, sizeof(fakeUserApi));
  fakeUserApi.messageArgsChecked = checked;
  fakeUserApi.messageArgsAllocate = fakeAllocate;
  fakeUserApi.messageArgsFree = fakeFree;
  fakeUserApi.messageBufferShare = fakeShare;
  fakeUserApi.messageBufferUnshare = fakeUnshare;
  fakeApi.executiveApi = &fakeExecutiveApi;
  testOverlayMap.header.osApi = &fakeApi;
}

typedef struct OverlayArgs {
  uint32_t value;
  char name[8];
} OverlayArgs;

NANO_OS_TEST(overlay_message_args, production_allocate_uses_its_own_storage) {
  useFakeApi(false);
  MESSAGE_ARGS_ALLOCATE(OverlayArgs, args);
  NANO_OS_ASSERT_EQ_PTR(&argsStorage, args);
  NANO_OS_ASSERT_EQ_INT(0, args->value);
  NANO_OS_ASSERT_EQ_INT(0, args->name[0]);
  MESSAGE_ARGS_FREE(args);
  NANO_OS_ASSERT_EQ_INT(0, calls.allocates);
  NANO_OS_ASSERT_EQ_INT(0, calls.frees);
}

NANO_OS_TEST(overlay_message_args, production_share_returns_the_buffer_unevaluated) {
  useFakeApi(false);
  char path[] = "/etc/passwd";
  int sizeEvaluations = 0;
  int recipientEvaluations = 0;
  char *shared = (char*) MESSAGE_BUFFER_SHARE(path,
    (sizeEvaluations++, sizeof(path)), MESSAGE_BUFFER_IN,
    (recipientEvaluations++, 7));
  NANO_OS_ASSERT_EQ_PTR(path, shared);
  NANO_OS_ASSERT_EQ_INT(0, sizeEvaluations);
  NANO_OS_ASSERT_EQ_INT(0, recipientEvaluations);
  MESSAGE_BUFFER_UNSHARE(shared, path, MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_EQ_INT(0, calls.shares);
  NANO_OS_ASSERT_EQ_INT(0, calls.unshares);
}

NANO_OS_TEST(overlay_message_args, checked_allocate_and_free_go_through_the_api) {
  useFakeApi(true);
  MESSAGE_ARGS_ALLOCATE(OverlayArgs, args);
  NANO_OS_ASSERT_EQ_PTR(fakeStorage, args);
  NANO_OS_ASSERT_EQ_INT(1, calls.allocates);
  NANO_OS_ASSERT_EQ_INT(sizeof(OverlayArgs), calls.allocateSize);
  MESSAGE_ARGS_FREE(args);
  NANO_OS_ASSERT_EQ_INT(1, calls.frees);
  NANO_OS_ASSERT_EQ_PTR(fakeStorage, calls.freed);
}

NANO_OS_TEST(overlay_message_args, checked_share_and_unshare_go_through_the_api) {
  useFakeApi(true);
  char path[] = "/etc/passwd";
  char *shared = (char*) MESSAGE_BUFFER_SHARE(path, sizeof(path),
    MESSAGE_BUFFER_INOUT, 7);
  NANO_OS_ASSERT_EQ_PTR(fakeShared, shared);
  NANO_OS_ASSERT_EQ_INT(1, calls.shares);
  NANO_OS_ASSERT_EQ_PTR(path, calls.sharePtr);
  NANO_OS_ASSERT_EQ_INT(sizeof(path), calls.shareSize);
  NANO_OS_ASSERT_EQ_INT(MESSAGE_BUFFER_INOUT, calls.shareDirection);
  NANO_OS_ASSERT_EQ_INT(7, calls.shareRecipient);
  MESSAGE_BUFFER_UNSHARE(shared, path, MESSAGE_BUFFER_INOUT);
  NANO_OS_ASSERT_EQ_INT(1, calls.unshares);
  NANO_OS_ASSERT_EQ_PTR(fakeShared, calls.unshareShared);
  NANO_OS_ASSERT_EQ_PTR(path, calls.unshareOriginal);
  NANO_OS_ASSERT_EQ_INT(MESSAGE_BUFFER_INOUT, calls.unshareDirection);
}

NANO_OS_TEST(overlay_message_args, user_processes_are_checked_too) {
  useFakeApi(true);
  fakeApi.executiveApi = NULL;
  char path[] = "/etc/passwd";
  NANO_OS_ASSERT_EQ_PTR(fakeShared,
    MESSAGE_BUFFER_SHARE(path, sizeof(path), MESSAGE_BUFFER_IN, 7));
  MESSAGE_ARGS_ALLOCATE(OverlayArgs, args);
  NANO_OS_ASSERT_TRUE(args != &argsStorage);
  NANO_OS_ASSERT_EQ_INT(1, calls.shares);
  NANO_OS_ASSERT_EQ_INT(1, calls.allocates);
  MESSAGE_ARGS_FREE(args);
  NANO_OS_ASSERT_EQ_INT(1, calls.frees);
}
