///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_message_args_production.c
///
/// @brief             Tests of the production expansions of MessageArgs.h,
///                    which this file is compiled to use even though the rest
///                    of the harness uses the checked ones.
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

#include <stdlib.h>
#include <string.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

#ifdef NANO_OS_MESSAGE_ARGS_CHECKED
#error "This suite must be compiled with the production expansions."
#endif

typedef struct ProductionInner {
  uint8_t bytes[3];
  void *pointer;
} ProductionInner;

typedef struct ProductionArgs {
  ProductionInner inner;
  char name[5];
  int errorNumber;
  void *returnValue;
} ProductionArgs;

/// @fn static void dirtyStack(void)
///
/// @brief Fill the stack below the caller with non-zero bytes, so storage a
/// later call declares there starts out dirty.
///
/// @return This function returns no value.
static __attribute__((noinline)) void dirtyStack(void) {
  volatile uint8_t junk[512];
  for (size_t ii = 0; ii < sizeof(junk); ii++) {
    junk[ii] = 0xee;
  }
}

/// @fn static bool allocatedArgsAreZero(void)
///
/// @brief Allocate production storage on a dirtied stack and check it's
/// zeroed.
///
/// @return Returns true if every byte of the storage was zero.
static __attribute__((noinline)) bool allocatedArgsAreZero(void) {
  MESSAGE_ARGS_ALLOCATE(ProductionArgs, args);
  if (args == NULL) {
    return false;
  }
  bool zero = (args->inner.pointer == NULL) && (args->returnValue == NULL);
  for (size_t ii = 0; ii < sizeof(args->inner.bytes); ii++) {
    zero = zero && (args->inner.bytes[ii] == 0);
  }
  for (size_t ii = 0; ii < sizeof(args->name); ii++) {
    zero = zero && (args->name[ii] == 0);
  }
  zero = zero && (args->errorNumber == 0);
  MESSAGE_ARGS_FREE(args);
  return zero;
}

NANO_OS_TEST(message_args_production, allocate_zeroes_dirty_stack_storage) {
  dirtyStack();
  NANO_OS_ASSERT_TRUE(allocatedArgsAreZero());
}

NANO_OS_TEST(message_args_production, allocate_points_at_its_own_storage) {
  MESSAGE_ARGS_ALLOCATE(ProductionArgs, args);
  NANO_OS_ASSERT_EQ_PTR(&argsStorage, args);
  args->errorNumber = 7;
  NANO_OS_ASSERT_EQ_INT(7, argsStorage.errorNumber);
  MESSAGE_ARGS_FREE(args);
}

/// @var _evaluations
///
/// @brief The number of times countedSize and countedRecipient have run.
static int _evaluations = 0;

/// @fn static size_t countedSize(void)
///
/// @brief A size argument whose evaluation the tests can see.
///
/// @return Returns 4.
static size_t countedSize(void) {
  _evaluations++;
  return 4;
}

/// @fn static ProcessId countedRecipient(void)
///
/// @brief A recipient argument whose evaluation the tests can see.
///
/// @return Returns 1.
static ProcessId countedRecipient(void) {
  _evaluations++;
  return 1;
}

NANO_OS_TEST(message_args_production, share_returns_the_buffer_unevaluated) {
  char buffer[] = "path";
  _evaluations = 0;
  NANO_OS_ASSERT_EQ_INT(4, (int) countedSize());
  NANO_OS_ASSERT_EQ_INT(1, (int) countedRecipient());
  NANO_OS_ASSERT_EQ_INT(2, _evaluations);
  _evaluations = 0;
  void *shared = MESSAGE_BUFFER_SHARE(buffer, countedSize(),
    MESSAGE_BUFFER_IN, countedRecipient());
  NANO_OS_ASSERT_EQ_PTR(buffer, shared);
  MESSAGE_BUFFER_UNSHARE(shared, buffer + countedSize(), MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_EQ_INT(0, _evaluations);
}

NANO_OS_TEST(message_args_production, create_is_zeroed_heap_storage) {
  ProductionArgs *args
    = MESSAGE_ARGS_CREATE(ProductionArgs, MESSAGE_ARGS_ROUND_TRIP);
  NANO_OS_ASSERT_NOT_NULL(args);
  NANO_OS_ASSERT_EQ_INT(0, args->errorNumber);
  NANO_OS_ASSERT_NULL(args->returnValue);
  MESSAGE_ARGS_DESTROY(args);

  uint8_t *bytes = (uint8_t*) MESSAGE_ARGS_CREATE_BYTES(37,
    MESSAGE_ARGS_ONE_WAY);
  NANO_OS_ASSERT_NOT_NULL(bytes);
  for (int ii = 0; ii < 37; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, bytes[ii]);
  }
  MESSAGE_ARGS_DESTROY(bytes);
}

/// @var _destroyed
///
/// @brief The pointer recordDestroy was last given.
static void *_destroyed = NULL;

/// @fn static void recordDestroy(void *ptr)
///
/// @brief A free function whose calls the tests can see.
///
/// @param ptr The pointer to record.
///
/// @return This function returns no value.
static void recordDestroy(void *ptr) {
  _destroyed = ptr;
}

NANO_OS_TEST(message_args_production, destroy_with_calls_the_function_given) {
  int storage = 0;
  _destroyed = NULL;
  MESSAGE_ARGS_DESTROY_WITH(&storage, recordDestroy);
  NANO_OS_ASSERT_EQ_PTR(&storage, _destroyed);
}

/// @fn int messageArgsProductionAdopt(ExecArgs *execArgs, ProcessId pid)
///
/// @brief Compile the production expansion of MESSAGE_ARGS_ADOPT, which only
/// the scheduler can run.
///
/// @param execArgs A block to adopt.
/// @param pid The process to adopt it for.
///
/// @return Returns what MESSAGE_ARGS_ADOPT returns.
int messageArgsProductionAdopt(ExecArgs *execArgs, ProcessId pid) {
  return MESSAGE_ARGS_ADOPT(execArgs, pid, (MessageArgsCopyFn) NULL);
}
