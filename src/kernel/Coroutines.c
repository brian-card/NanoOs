////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/*
 * Some notes about the way(s) this library works.
 *
 * Tony Finch is an extremely clever programmer and I am forever indebted to
 * him for his initial work on this.  His approach to this is, in my opinion,
 * sheer genius.  It is, however, not straightforward and requires some
 * explanation.
 *
 * This approach makes use of two stacks:  A stack of running coroutines and a
 * stack of idle coroutines.  Prior to coroutineConfig being called, both stacks
 * are empty.  coroutineConfig initializes the running stack to the Coroutine
 * passed in and sets the stack size that is to be used, but leaves the idle
 * stack alone.  The idle stack is not modified until the first call to
 * coroutineInit is made.  From that point on, there is always at least one
 * Coroutine on the idle stack.
 *
 * When coroutineInit is called, it first checks the idle stack to see if
 * there is anything available.  The only time there won't be anything available
 * is on the first time this function is called (per thread).  If nothing is
 * available, coroutineAllocateStack is called with the stack size that was
 * provided to coroutineConfig.  This will allocate the remainder of the stack
 * for the main function, allocate a Coroutine structure, push a pointer to it
 * onto the idle stack, and then resume execution in coroutineInit.  If a
 * Coroutine is available when coroutineInit does its check, it simply pops
 * the stack.
 *
 * Once coroutineInit has an idle Coroutine, it pushes the Coroutine onto the
 * running stack and then passes it the function pointer it was provided.  This
 * resumes execution in the coroutineMain function.  There are two possible
 * places in coroutineMain where execution may resume.  If the Coroutine that
 * was pulled from the idle stack had been used previously and completed, then
 * execution resumes at the end of the main while loop where the function is
 * picked up as the value returned from its yield call.  However, if this is the
 * Coroutine that was last created, then its point of entry will be near the
 * beginning of coroutineMain.
 *
 * In the former case, coroutineMain just yields back to the calling Coroutine
 * and waits for its arguments to be provided by coroutineResume.  In the latter
 * case, things aren't so simple.  In this case, the provided function pointer
 * is stored and then coroutineAllocateStack is called again to allocate another
 * Coroutine.  As before, this coroutine is pushed onto the idle stack, however
 * when it does a longjmp this time, execution is not resumed in the
 * coroutineInit constructor but in the previous-level coroutineMain call.
 * This then enters the main while loop which yields control back to the calling
 * coroutine and waits for its arguments to be provided (just as in the former
 * case above).
 *
 * From this point onward, passing control between coroutines is done by a
 * running coroutine calling coroutineYield to yield control back to its caller
 * and a calling function calling coroutineResume to resume execution in the
 * called coroutine from the point at which the coroutine made its last
 * coroutineYield call (or from the beginning of the function if the function
 * has not yet begun execution).
 *
 * coroutineInit, coroutineMain, coroutineResume, and coroutineYield make use
 * of an internal function called coroutinePass and/or raw setjmp and longjmp
 * function calls.  coroutinePass also makes use of setjmp and longjmp.
 * coroutinePass takes as parameters a pointer to the Coroutine making the call
 * and the value that is to be passed to the Coroutine whose execution is
 * resumed.  Execution will be passed to the Coroutine that is at the top of the
 * running stack.  It is the responsibility of the caller to ensure that the
 * running stack is setup correctly before calling coroutinePass.
 *
 * The second parameter of coroutinePass is a CoroutineFuncData union.  This is
 * a union of a data pointer and a function pointer.  The reason for this union
 * is that both function pointers and data pointers have to be passed through
 * coroutinePass, however the C standard forbids casting function pointers to
 * data pointers and vice-versa.  So, we need a union in order to be compliant
 * with the specification.  It is the responsibility of the caller to know which
 * kind of value to provide and which kind of value to expect.  Note that
 * coroutineResume and coroutineYield only deal with data pointers, so the
 * function pointer use case can only be exercised by functions within this
 * library (because coroutinePass is not an externally-visible function).  This
 * is deliberate and this is what is desirable.
 *
 * Some implications and limitations...
 *
 * The fact that there is always at least one Coroutine on the idle stack has
 * some implications for memory-constrained environments.  The first time that
 * coroutineInit is called, the upper limit for the calling function's stack
 * -AND- the stack for the created Coroutine are both allocated.  Allocation
 * is achieved by recursively calling a function with a character buffer, which
 * means that the memory in those stacks will be touched during this process.
 * Effectively, the first time that coroutineInit is called, a little over 2X
 * the stack size provided to coroutineConfig is touched.  This can result in
 * a nasty surprise (i.e. a crash) in severely memory-constrained environments.
 *
 * One requirement of this system is that all the stacks must be the same size.
 * The stack size provided to coroutineConfig cannot be changed once the first
 * Coroutine has been created with coroutineInit.  This is because the size
 * of a stack for a Coroutine is actually provided to coroutineAllocateStack on
 * the call prior to the call that returns a usable Coroutine.  It would be very
 * difficult (and highly confusing) to try and adjust the size dynamically.  To
 * save us all any more confusion than this system already causes, all stacks on
 * a single thread must be the same size.
 *
 * JBC 2024-11-18
 */

// Doxygen marker
/// @file

#include "stdio.h" // For error messages
#include "string.h"
#include "stdlib.h"

#include "Coroutines.h"

// Prototype forward declarations.
void coroutineAllocateStack(int stackSize);
void coroutineMain(void *stack);
int comessageQueueCreate(Coroutine *coroutine);
int comessageQueueDestroy(Coroutine *coroutine);

/// @def ZEROINIT
///
/// @brief Define the proper way to declare a zeroized variable based on the
/// compiler environment.
#ifdef __cplusplus
#ifdef _MSC_VER
#define ZEROINIT(x) x = {0}
#else // Non-Visual Studio C++
#define ZEROINIT(x) x = {}
#endif // _MSC_VER
#else // __cplusplus not defined
#define ZEROINIT(x) x = {0}
#endif // __cplusplus

// Use statically-allocated global variables for use without threading.

/// @var static Coroutine *_globalFirst
///
/// @brief Library-private first (main) routine.
static Coroutine *_globalFirst = NULL;

/// @var static Coroutine *_globalRunning
///
/// @brief Library-private head of the running list.
///
/// @details The coroutine at the head of the running LIFO list has the CPU, and
/// all others are suspended inside coroutineResume(). The first Coroutine
/// object holds the context for the process's initial stack and also ensures
/// that all externally-visible list elements have non-NULL next pointers.
static Coroutine* _globalRunning = NULL;

/// @var static Coroutine *_globalIdle
///
/// @brief Library-private head of the idle list.
///
/// @details The idle LIFO list contains coroutines that are suspended in
/// coroutineMain() and available to be associated with a new function. After
/// initialization it is never NULL except briefly while coroutineMain() forks
/// a new idle coroutine.
static Coroutine* _globalIdle = NULL;

/// @var static int _globalStackSize
///
/// @brief The size of each coroutine's stack in bytes.
static int _globalStackSize = COROUTINE_DEFAULT_STACK_SIZE;

/// @var _globalStateData
///
/// @brief Global state data provided to the global callbacks.
static void *_globalStateData = NULL;

/// @var static CoroutineYieldCallback _globalCoroutineYieldCallback
///
/// @brief Global callback to call when a comutex is unlocked.
static CoroutineYieldCallback _globalCoroutineYieldCallback = NULL;

/// @var static ComutexUnlockCallback _globalComutexUnlockCallback
///
/// @brief Global callback to call when a comutex is unlocked.
static ComutexUnlockCallback _globalComutexUnlockCallback = NULL;

/// @var static CoconditionSignalCallback _globalCoconditionSignalCallback
///
/// @brief Global callback to call when a cocondition is signalled.
static CoconditionSignalCallback _globalCoconditionSignalCallback = NULL;

/// @fn int64_t coroutineGetNanoseconds(const struct timespec *ts)
///
/// @brief Convert the time in a timespec to a raw number of nanoseconds.
///
/// @param ts A pointer to the struct timespec to convert.  If this value is
///   NULL, the current time will be used.
///
/// @return Returns the number of nanoseconds since midnight, Jan 1, 1970
/// represented by the timespec.
int64_t coroutineGetNanoseconds(const struct timespec *ts) {
  struct timespec timespecStorage = { 0, 0 };

  if (ts == NULL) {
    timespec_get(&timespecStorage, TIME_UTC);
    ts = &timespecStorage;
  }

  return (((int64_t) ts->tv_sec) * ((int64_t) 1000000000LL))
         + ((int64_t) ts->tv_nsec);
}

/// @fn void coroutineGlobalPush(Coroutine **list, Coroutine *coroutine)
///
/// @brief Add a coroutine to a global list and get the previous head of the
/// list.
///
/// @param list The Coroutine list to push onto.
/// @param coroutine A pointer to the Coroutine to push onto the list.
///
/// @param Returns the previous head of the list.
void coroutineGlobalPush(Coroutine** list, Coroutine* coroutine) {
  if ((list != NULL) && (coroutine != NULL)) {
    coroutine->nextInList = *list;
    *list = coroutine;
  }
}

/// @fn Coroutine* coroutineGlobalPop(Coroutine **list)
///
/// @brief Remove a coroutine from a global list and return it.
///
/// @param list The Coroutine list to pop from.
///
/// @return Returns a pointer to the previous head of the list.
Coroutine* coroutineGlobalPop(Coroutine** list) {
  Coroutine* coroutine = NULL;

  if (list != NULL) {
    coroutine = *list;
    *list = coroutine->nextInList;
    coroutine->nextInList = NULL;
  }

  return coroutine;
}

#ifdef THREAD_SAFE_COROUTINES
// Use dynamic memory and C threads mechanisms.

/// @var static bool _coroutineThreadingSupportEnabled
///
/// @brief State variable to determine whether or not threading support is
/// enabled in systems where threading is possible.  Defaults to false.
static bool _coroutineThreadingSupportEnabled = false;

/// @var static tss_t _tssFirst
///
/// @brief Thread-specific first (main) routine.
ZEROINIT(static tss_t _tssFirst);

/// @var static tss_t _tssRunning
///
/// @brief Thread-specific head of the running list.
///
/// @details The coroutine at the head of the running LIFO list has the CPU, and
/// all others are suspended inside coroutineResume(). The "first" coro object
/// holds the context for the process's initial stack and also ensures that all
/// externally-visible list elements have non-NULL next pointers.  (The "first"
/// coroutine isn't exposed to the caller.)
ZEROINIT(static tss_t _tssRunning);

/// @var static tss_t _tssIdle
///
/// @brief Thread-specific head of the idle list.
///
/// @details The idle LIFO list contains coroutines that are suspended in
/// coroutineMain() and available to be associated with a new function. After
/// initialization it is never NULL except briefly while coroutineMain() forks
/// a new idle coroutine.
ZEROINIT(static tss_t _tssIdle);

/// @var static tss_t _tssStackSize
///
/// @brief Thread-specific size of each coroutine's stack, in bytes.
ZEROINIT(static tss_t _tssStackSize);

/// @var static tss_t _tssStateData
///
/// @brief Thread-specific state data provided to the thread-specific callbacks.
ZEROINIT(static tss_t _tssStateData);

/// @var static tss_t _tssCoroutineYieldCallback
///
/// @brief Thread-specific callback to call when a cocondition is signalled.
ZEROINIT(static tss_t _tssCoroutineYieldCallback);

/// @var static tss_t _tssComutexUnlockCallback
///
/// @brief Thread-specific callback to call when a cocondition is signalled.
ZEROINIT(static tss_t _tssComutexUnlockCallback);

/// @var static tss_t _tssCoconditionSignalCallback
///
/// @brief Thread-specific callback to call when a cocondition is signalled.
ZEROINIT(static tss_t _tssCoconditionSignalCallback);

/// @var static once_flag _threadMetadataSetup
///
/// @brief once_flag to make sure we only initialize the thread-specific storage
/// once.
static once_flag _threadMetadataSetup = ONCE_FLAG_INIT;

/// @fn void coroutineSetupThreadMetadata(void)
///
/// @brief Setup the global thread-specific storage needed for this library.
///
/// @return This function returns no value.
void coroutineSetupThreadMetadata(void) {
  int status = tss_create(&_tssFirst, NULL);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssFirst.\n");
  }
  status = tss_create(&_tssRunning, NULL);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssRunning.\n");
  }
  status = tss_create(&_tssIdle, NULL);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssIdle.\n");
  }
  status = tss_create(&_tssStackSize, NULL);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssStackSize.\n");
  }
  status = tss_create(&_tssStateData, NULL);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssStateData.\n");
  }
  status = tss_create(&_tssCoroutineYieldCallback, free);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssCoroutineYieldCallback.\n");
  }
  status = tss_create(&_tssComutexUnlockCallback, free);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssComutexUnlockCallback.\n");
  }
  status = tss_create(&_tssCoconditionSignalCallback, free);
  if (status != thrd_success) {
    fprintf(stderr, "Could not initialize _tssCoconditionSignalCallback.\n");
  }
}

/// @fn void coroutineInitializeThreadMetadata(Coroutine *first)
///
/// @brief Initialize the thread-specific storage on a per-thread basis.
///
/// @param first The coroutine to use as the first coroutine on the current
///   thread.  This value may not be NULL on the first call to this function
///   on the thread.
///
/// @return Returns true on success, false on failure.
bool coroutineInitializeThreadMetadata(Coroutine *first) {
  if (tss_get(_tssFirst) != NULL) {
    // This thread is already set up.  Nothing to do.
    return true;
  }

  if (first == NULL) {
    fprintf(stderr, "ERROR:  NULL first Coroutine provided to "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }

  int status = tss_set(_tssFirst, first);
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssFirst to first in "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }
  status = tss_set(_tssRunning, first);
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssRunning to first in "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }
  // This *shouldn't* be necessary, but why assume when we can be certain?
  status = tss_set(_tssIdle, NULL);
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssIdle to NULL in coroutineInitializeThreadMetadata.\n");
    return false;
  }
  status = tss_set(_tssStackSize, (void*) ((intptr_t) _globalStackSize));
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssStackSize to %d in "
      "coroutineInitializeThreadMetadata.\n", _globalStackSize);
    return false;
  }
  status = tss_set(_tssStateData, _globalStateData);
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssStateData to %p in "
      "coroutineInitializeThreadMetadata.\n", _globalStateData);
    return false;
  }
  status = tss_set(
    _tssCoroutineYieldCallback,
    NULL
  );
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssCoroutineYieldCallback to NULL in "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }
  status = tss_set(
    _tssComutexUnlockCallback,
    NULL
  );
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssComutexUnlockCallback to NULL in "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }
  status = tss_set(
    _tssCoconditionSignalCallback,
    NULL
  );
  if (status != thrd_success) {
    fprintf(stderr,
      "Could not set _tssCoconditionSignalCallback to NULL in "
      "coroutineInitializeThreadMetadata.\n");
    return false;
  }

  return true;
}

/// @fn void coroutineTssPush(tss_t* list, Coroutine *coroutine)
///
/// @brief Add a coroutine to a thread-specific storage list and get the
/// previous head of the list.
///
/// @param list The Coroutine list to push onto.
/// @param coroutine A pointer to the Coroutine to push onto the list.
///
/// @param Returns the previous head of the list.
void coroutineTssPush(tss_t* list, Coroutine* coroutine) {
  if ((list != NULL) && (coroutine != NULL)) {
    coroutine->nextInList = (Coroutine*) tss_get(*list);
    tss_set(*list, coroutine);
  }
}

/// @fn Coroutine* coroutineTssPop(tss_t* list)
///
/// @brief Remove a coroutine from a thread-specific storage list and return it.
///
/// @param list The Coroutine list to pop from.
///
/// @return Returns a pointer to the previous head of the list.
Coroutine* coroutineTssPop(tss_t* list) {
  Coroutine* coroutine = NULL;

  if (list != NULL) {
    coroutine = (Coroutine*) tss_get(*list);
    tss_set(*list, coroutine->nextInList);
    coroutine->nextInList = NULL;
  }

  return coroutine;
}

#endif // THREAD_SAFE_COROUTINES

/// @def coroutinePushRunning
///
/// @brief Push a coroutine onto the appropriate running stack.
#ifdef THREAD_SAFE_COROUTINES

#define coroutinePushRunning(coroutine) \
  ((_coroutineThreadingSupportEnabled) \
  ? coroutineTssPush(&_tssRunning, coroutine) \
  : coroutineGlobalPush(&_globalRunning, coroutine))

#else

#define coroutinePushRunning(coroutine) \
  coroutineGlobalPush(&_globalRunning, coroutine)

#endif // THREAD_SAFE_COROUTINES coroutinePushRunning

/// @def coroutinePushIdle
///
/// @brief Push a coroutine onto the appropriate idle stack.
#ifdef THREAD_SAFE_COROUTINES

#define coroutinePushIdle(coroutine) \
  ((_coroutineThreadingSupportEnabled) \
  ? coroutineTssPush(&_tssIdle, coroutine) \
  : coroutineGlobalPush(&_globalIdle, coroutine))

#else

#define coroutinePushIdle(coroutine) \
  coroutineGlobalPush(&_globalIdle, coroutine)

#endif // THREAD_SAFE_COROUTINES coroutinePushIdle

/// @def coroutinePopRunning
///
/// @brief Pop a coroutine from the appropriate running stack.
#ifdef THREAD_SAFE_COROUTINES

#define coroutinePopRunning() \
  ((_coroutineThreadingSupportEnabled) \
  ? coroutineTssPop(&_tssRunning) \
  : coroutineGlobalPop(&_globalRunning))

#else

#define coroutinePopRunning() \
  coroutineGlobalPop(&_globalRunning);

#endif // THREAD_SAFE_COROUTINES coroutinePopRunning

/// @def coroutinePopIdle
///
/// @brief Pop a coroutine from the appropriate idle stack.
#ifdef THREAD_SAFE_COROUTINES

#define coroutinePopIdle() \
  ((_coroutineThreadingSupportEnabled) \
  ? coroutineTssPop(&_tssIdle) \
  : coroutineGlobalPop(&_globalIdle))

#else

#define coroutinePopIdle() \
  coroutineGlobalPop(&_globalIdle);

#endif // THREAD_SAFE_COROUTINES coroutinePopIdle

/// @fn Coroutine* getRunningCoroutine(void)
///
/// @brief Get a pointer to the Coroutine that is currently running (the one
/// that is at the top of the running stack.
///
/// @return Returns a pointer to the currently-running Coroutine on success,
/// NULL on failure (if coroutines haven't been initialized yet).
Coroutine* getRunningCoroutine(void) {
  Coroutine *coroutine = _globalRunning;
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      coroutine = NULL;
      return coroutine;
    }
    coroutine = (Coroutine*) tss_get(_tssRunning);
  }
#endif
  
  return coroutine;
}

/// @fn void* coroutinePass(Coroutine currentCoroutine, CoroutineFuncData arg)
///
/// @brief Pass a value and control from one coroutine to another.  The target
/// coroutine is at the head of the "running" list.
///
/// @param currentCoroutine A pointer to the current coroutine's state.
/// @param arg The value to pass to the target coroutine.
///
/// @return Returns the target's returned or yielded value.
CoroutineFuncData coroutinePass(Coroutine *currentCoroutine, CoroutineFuncData arg) {
  ZEROINIT(CoroutineFuncData returnValue);
  
  if (currentCoroutine != NULL) {
    if (!setjmp(currentCoroutine->context)) {
      Coroutine* targetCoroutine = getRunningCoroutine();

      if (targetCoroutine != NULL) {
#if defined(_MSC_VER) && defined(_M_X64)
        // This should *NOT* be necessary.  The intent of longjmp is to restore
        // the context of the registers captured at setjmp, however the MSVC x64
        // implementation of longjmp only does this if the value of
        // _JUMP_BUFFER.Frame is 0.  This is a non-standard and broken
        // implementation, but thankfully a workaround does exist, so I won't
        // complain beyond this comment.
        _JUMP_BUFFER* context = (_JUMP_BUFFER*) &targetCoroutine->context;
        context->Frame = 0;
#endif // _MSC_VER
        targetCoroutine->passed = arg;
        longjmp(targetCoroutine->context, 1);
      }
    }
  } else {
    return returnValue; // NULL return value.
  }

  returnValue = currentCoroutine->passed;
  return returnValue;
}

/// @fn void* coroutineResume(Coroutine *targetCoroutine, void *arg)
///
/// @brief Transfer control to another coroutine.  A coroutine that is blocked
/// inside coroutineResume() is not resumable.
///
/// @param targetCoroutine A pointer to the Coroutine to resume.
/// @param arg Returned by coroutineYield() inside the target coroutine or
///   passed as the coroutine's parameter on the first call to coroutineResume
///   for the coroutine.
///
/// @return If the coroutine is resumable, returns the value provided to the
///   yield call from within the coroutine or the coroutine's return value if it
///   has run to completion.  Returns COROUTINE_ERROR on error.
void* coroutineResume(Coroutine *targetCoroutine, void *arg) {
  if (targetCoroutine == NULL) {
    return COROUTINE_ERROR;
  }
  
  if (coroutineCorrupted(targetCoroutine)) {
    return COROUTINE_ERROR;
  }
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return NULL;
    }
  }
#endif

  if (coroutineResumable(targetCoroutine)) {
    Coroutine *currentCoroutine = getRunningCoroutine();

    if (currentCoroutine == NULL) {
      // The running stack hasn't been defined yet.  Bail.
      return NULL;
    }

    coroutinePushRunning(targetCoroutine);
    // The target coroutine is now at the head of the running list as is
    // expected by coroutinePass().
    CoroutineFuncData funcData;
    funcData.data = arg;
    funcData = coroutinePass(currentCoroutine, funcData);

    return funcData.data;
  }

  return COROUTINE_ERROR;
}

/// @fn void* coroutineYield_(void *arg, CoroutineState state)
///
/// @brief Transfer control back to the coroutine that resumed this one.  A
/// coroutine that is blocked inside coroutineYield() may be resumed by any
/// other coroutine.
///
/// @param arg Value that will be returned by coroutineResume().
/// @param state The state to set the coroutine to before control is returned
///   to the caller.
///
/// @note This function is wrapped by a function macro of the same name (minus
/// the trailing underscore) that automatically casts the state parameter to
/// a CoroutineState so that integers may be used for the parameter.
///
/// @return Returns the value passed into the next call to coroutineResume()
/// for this coroutine.
void* coroutineYield_(void *arg, CoroutineState state) {
  void *returnValue = NULL;
  Coroutine* first = _globalFirst;
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return NULL;
    }
    first = (Coroutine*) tss_get(_tssFirst);
  }
#endif

  Coroutine *running = getRunningCoroutine();
  if (running == first) {
    // Can't yield from the main coroutine.  Return NULL.
    return NULL;
  } else if (running == NULL) {
    // The running stack hasn't been setup yet.  Bail.
    return NULL;
  }

  CoroutineYieldCallback coroutineYieldCallback
    = _globalCoroutineYieldCallback;
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    // No need to call coroutineSetupThreadMetadata or
    // coroutineInitializeThreadMetadata this time since we did that above.
    CoroutineYieldCallback *possibleCallback
      = (CoroutineYieldCallback*) tss_get(_tssCoroutineYieldCallback);
    if (possibleCallback != NULL) {
      coroutineYieldCallback = *possibleCallback;
    }
  }
#endif
  if (coroutineYieldCallback != NULL) {
    // coroutineYield is in the critical path of everything, so we only want to
    // get the state data if there's a function to call.
    void *stateData = _globalStateData;
#ifdef THREAD_SAFE_COROUTINES
    if (_coroutineThreadingSupportEnabled) {
      stateData = tss_get(_tssStateData);
    }
#endif
    coroutineYieldCallback(stateData, running);
  }

  if (state >= COROUTINE_STATE_NOT_RUNNING) {
    // This is not a state that's settable by the user.
    state = COROUTINE_STATE_BLOCKED;
  }

  Coroutine *currentCoroutine = coroutinePopRunning();
  currentCoroutine->state = state;
  CoroutineFuncData funcData;
  funcData.data = arg;
  funcData = coroutinePass(currentCoroutine, funcData);
  currentCoroutine->state = COROUTINE_STATE_RUNNING;
  returnValue = funcData.data;

  return returnValue;
}

/// @fn Coroutine* coroutineInit(Coroutine *userCoroutine, CoroutineFunction func, void *arg)
///
/// @brief The coroutine initialization function.
///
/// @details Initialize a coroutine that will run func(). The coroutine starts
/// off suspended.  The 'arg' argument provided to this function will be passed
/// to func() on the first call to coroutineResume.  If func() returns, its
/// return value is returned by coroutineResume() as if the coroutine yielded,
/// except that the coroutine is then no longer resumable and may be discarded
/// (*NOT* freed since its allocation is on the stack and not the heap).
///
/// On the first invocation there are no idle coroutines, so fork the first one,
/// which will immediately yield back to us after becoming idle. When there are
/// idle coroutines, we pass one the function pointer and return the activated
/// coroutine's address.
///
/// @param userCoroutine A coroutine provided by the user to configure instead
///   of creating a new one.  If this parameter is NULL, one from the idle list
///   will be used.  If the list is empty then a new one will be created.
/// @param func The coroutine function to run.
/// @param arg The initial argument to pass into the coroutine.
///
/// @return Returns a newly-initialized Coroutine on success, NULL on failure.
Coroutine* coroutineInit(Coroutine *userCoroutine,
  CoroutineFunction func, void *arg
) {
  CoroutineFuncData funcData;
  funcData.func = func;
  if (funcData.data == NULL) {
    // Can't pass a NULL function pointer (this would crash).  Return a NULL
    // Coroutine.  This will cause any future calls with this returned value to
    // coroutineResumable to return false and calls to coroutineResume to return
    // COROUTINE_NOT_RESUMABLE.
    return NULL;
  }

  Coroutine* idle = _globalIdle;
  Coroutine* running = getRunningCoroutine();
  int stackSize = _globalStackSize;
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return NULL;
    }
    stackSize = (int) ((intptr_t) tss_get(_tssStackSize));
    idle = (Coroutine*) tss_get(_tssIdle);
  }
#endif

  if (running == NULL) {
    fprintf(stderr, "ERROR: No first coroutine defined.\n");
    fprintf(stderr, "       Cannot create coroutine.\n");
    return NULL;
  }

  // The current coroutine is at the head of the running list.
  if ((idle == NULL) && (!setjmp(running->context))) {
    // We've just been called from the calling function and need to create a
    // new Coroutine instance, including its stack.
    coroutineAllocateStack(stackSize);
  }
  // Either there was an idle coroutine on the idle list or we just returned
  // from coroutineMain (called by coroutineAllocateStack).  Either way, the
  // Coroutine instance we want to use is now at the head of the idle list.

  Coroutine *configuredCoroutine = NULL;
  if (userCoroutine == NULL) {
    // This is the expected case.  The head of the idle list has the Coroutine
    // allocated in coroutineMain.
    configuredCoroutine = coroutinePopIdle();
  } else {
    // Use what the user provided.  By definition, idle must be non-NULL in
    // this case.  We need to remove the user's coroutine from the idle list.
    // Otherwise, when it completes, it will be on the list twice.
    bool found = false;
    if (idle != userCoroutine) {
      // Go through the list until we find the couroutine and adjust
      // accordingly.
      for (Coroutine *cur = idle; cur != NULL; cur = cur->nextInList) {
        if (cur->nextInList == userCoroutine) {
          cur->nextInList = userCoroutine->nextInList;
          found = true;
          break;
        }
      }
    } else {
      // Just pop the idle stack.
      found = true;
      coroutinePopIdle();
    }

    configuredCoroutine = userCoroutine;
    if (found == false) {
      // The user has provided an uninitialized coroutine.  Pop one from idle.
      configuredCoroutine = coroutinePopIdle();
    }
  }

  // The head of the running list is the current coroutine.
  // We need to run coroutineResume() and pass in the function pointer we were
  // provided as the argument to the coroutine, however coroutineResume() takes
  // a void* as its argument, not a function pointer.  coroutinePass() takes
  // the union of a data pointer and a function pointer, so it's legal to pass
  // a function pointer to that.  So, the logic below is the logic of
  // coroutineResume() with the appropriate variable substitution done so that
  // we can legally pass a function pointer and retrieve the Coroutine pointer.
  // We don't need to do the thread setup logic at the start of the function
  // since that's done above and we know that coroutine is resumable, so we
  // can skip that check.
  Coroutine *currentCoroutine = getRunningCoroutine();
  coroutinePushRunning(configuredCoroutine);
  // The target coroutine is now at the head of the running list as is
  // expected by coroutinePass().  funcData contains the coroutine that was
  // passed to this function.
  funcData = coroutinePass(currentCoroutine, funcData);
  configuredCoroutine = (Coroutine*) funcData.data;
  configuredCoroutine->nextToSignal = NULL;
  configuredCoroutine->prevToSignal = NULL;
  configuredCoroutine->blockingCocondition = NULL;
  configuredCoroutine->nextToLock = NULL;
  configuredCoroutine->prevToLock = NULL;
  configuredCoroutine->blockingComutex = NULL;

  coroutineResume(configuredCoroutine, arg);

  return configuredCoroutine;
}

/// @fn int coroutineCreate(Coroutine **coroutine, CoroutineFunction func, void *arg)
///
/// @brief Coroutine constructor.  This either creates and initializes a new
/// coroutine or pulls one off the idle list and initializes it.
/// After the coroutine is initialized with the provided function,
/// coroutineResume is called with the provided argument to pass the argument
/// into the coroutine.
///
/// @note This is mostly for compatibility with thrd_create in the C threads
/// specification.
///
/// @param coroutine A pointer to a Coroutine pointer to initialize if this call
///   is successful.  If *coroutine is non-NULL, the coroutine pointed to will
///   be reinitialized.  Otherwise, a Coroutine object will be pulled off the
///   idle list or created and then initialized.
/// @param func A function pointer to the function to run as a coroutine.
/// @param arg The initial argument to pass into the coroutine.
///
/// @return Returns coroutineSuccess on success, coroutineError on failure.
int coroutineCreate(Coroutine **coroutine, CoroutineFunction func, void *arg) {
  if (coroutine == NULL)  {
    return coroutineError;
  }

  Coroutine *newCoroutine = coroutineInit(*coroutine, func, arg);
  if (newCoroutine == NULL) {
    return coroutineError;
  }
  *coroutine = newCoroutine;

  return coroutineSuccess;
}

/// void coroutineMain(void *stack)
///
/// @brief The main loop responsible for managing the "idle" list.
///
/// @details When we start the idle list is empty, so we put ourself on it to
/// ensure it remains non-NULL. Then we immediately suspend ourself waiting for
/// the first function we are to run. (The head of the running list is the
/// coroutine that forked us.) We pass the stack pointer to prevent it from
/// being optimised away. The first time we are called we will return to the
/// fork in the coroutineInit() constructor function (above); on subsequent
/// calls we will resume the parent coroutineMain(). In both cases the passed
/// value is lost when coroutinePass() longjmp()s to the forking setjmp().
///
/// When we are resumed, the idle list is empty again, so we fork another
/// coroutine. When the child coroutineMain() passes control back to us, we drop
/// into our main loop.
///
/// We are now head of the running list with a function to call. We immediately
/// yield a pointer to our context object so our creator can identify us. The
/// creator can then resume us at which point we pass the argument to the
/// function to start executing.
///
/// When the function returns, we move ourself from the running list to the idle
/// list, before passing the result back to the resumer. (This is just like
/// coroutineYield() except for adding the coroutine to the idle list.) We can
/// then only be resumed by the coroutineInit() constructor function which
/// will put us back on the running list and pass us a new function to call.
///
/// @param stack A pointer to the stack that was allocated for the coroutine.
///   This parameter is taken from the stack allocation function so that the
///   stack allocated there doesn't get optimized out by the compiler.
///
/// @return This function returns no value and, in fact, never returns.
void coroutineMain(void *stack) {
  ZEROINIT(Coroutine me);
  me.guard1 = COROUTINE_GUARD_VALUE;
  me.guard2 = COROUTINE_GUARD_VALUE;
  coroutinePushIdle(&me);

  // Initialize the message queue.
  comessageQueueCreate(&me);

  // The target of coroutinePass() (the caller) is at the head of the running
  // list.  The return point for that Coroutine was either set in the setjmp
  // call in the coroutine constructor or in the setjmp call below.  In the
  // former case, calling coroutinePass() here returns to the constructor and
  // waits for the constructor to provide the function pointer to call.  In the
  // latter case, we allocate the next Coroutine and its stack that will be
  // pushed onto the idle list (above).  When we call pass here, we will release
  // the Coroutine currently being constructed from the setjmp below, allowing
  // it to drop into its main loop and we will be on the idle stack ready to
  // take in a new function pointer when we're resumed.
  CoroutineFuncData funcData;
  funcData.data = stack;
  funcData = coroutinePass(&me, funcData);
  void *(*func)(void *arg);
  func = funcData.func;

  // At this point, we've been passed execution from the constructor calling
  // coroutineResume().  coroutineResume() pushed the new coroutine (the one
  // we're in the middle of constructing that was declared as "me" above) onto
  // the running list before returning control to us.  So the return point
  // we're about to set is for ourself.  The call to coroutineAllocateStack here
  // will allocate the next Coroutine on the idle list to be used in the next
  // call to the constructor.
  Coroutine *running = getRunningCoroutine();
  int stackSize = _globalStackSize;
#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    stackSize = (int) ((intptr_t) tss_get(_tssStackSize));
  }
#endif
  if (!setjmp(running->context)) {
    coroutineAllocateStack(stackSize);
  }

  if (setjmp(running->resetContext)) {
    // When a coroutine is killed, its normal context is set to this position
    // so that it can be restarted properly from the constructor.  We'll have to
    // manually pull the data that was provided from coroutinePass since the
    // constructor will be thinking that it just provided us with the function
    // we should call.
    funcData = running->passed;
    func = funcData.func;
  }

  // We have just been passed execution from the coroutinePass() statement
  // above.  The stack is now configured and we're ready to begin execution.
  // We will first yield the Coroutine allocated above that the constructor is
  // waiting on and then call the function we were passed.  When the function
  // ends, we will place ourselves on the idle list and can be reused by a
  // future invocation of the coroutineInit() constructor.
  while (1) {
    // Return our Coroutine and get the function argument from the constructor.
    // coroutineYield will set our state to BLOCKED on call and RUNNING on
    // return.
    void* callingArgument = coroutineYield(&me, COROUTINE_STATE_BLOCKED);

    // Yield again and wait to be resumed by the caller of coroutineInit.
    coroutineYield(NULL, COROUTINE_STATE_BLOCKED);

    // Call the target function with the calling argument.
    void* ret = func(callingArgument);

    // Deallocate the currently running coroutine and make it available to the
    // next iteration of the constructor.
    Coroutine *currentCoroutine = coroutinePopRunning();
    currentCoroutine->state = COROUTINE_STATE_NOT_RUNNING;
    coroutinePushIdle(currentCoroutine);

    // Destroy any messages that were sent.
    comessageQueueDestroy(&me);

    // Block until we're called from the constructor again.
    funcData.data = ret;
    funcData = coroutinePass(&me, funcData);
    // Re-initialize the queue.
    comessageQueueCreate(&me);
    func = funcData.func;
  }
}

void coroutineAllocateStack32(int stackSize, void *topOfStack);
void coroutineAllocateStack64(int stackSize, void *topOfStack);
void coroutineAllocateStack128(int stackSize, void *topOfStack);
void coroutineAllocateStack256(int stackSize, void *topOfStack);
void coroutineAllocateStack512(int stackSize, void *topOfStack);
void coroutineAllocateStack1024(int stackSize, void *topOfStack);

/// @def allocateNextStackChunk
///
/// @brief Common macro for determining which stack allocation function to call
/// next and calling it.
#define allocateNextStackChunk(stackSize, topOfStack) \
  if (topOfStack == NULL) { \
    topOfStack = stack; \
  } \
   \
  stackSize -= sizeof(stack); \
  if (stackSize >= 1024) { \
    coroutineAllocateStack1024(stackSize, topOfStack); \
  } else if (stackSize >= 512) { \
    coroutineAllocateStack512(stackSize, topOfStack); \
  } else if (stackSize >= 256) { \
    coroutineAllocateStack256(stackSize, topOfStack); \
  } else if (stackSize >= 128) { \
    coroutineAllocateStack128(stackSize, topOfStack); \
  } else if (stackSize >= 64) { \
    coroutineAllocateStack64(stackSize, topOfStack); \
  } else if (stackSize >   0) { \
    coroutineAllocateStack32(stackSize, NULL); \
  } \
   \
  coroutineMain(topOfStack)

/// void coroutineAllocateStack32(int stackSize, void *topOfStack)
///
/// @brief Allocate 32 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack32(int stackSize, void *topOfStack) {
  char stack[32];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack64(int stackSize, void *topOfStack)
///
/// @brief Allocate 64 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack64(int stackSize, void *topOfStack) {
  char stack[64];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack128(int stackSize, void *topOfStack)
///
/// @brief Allocate 128 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack128(int stackSize, void *topOfStack) {
  char stack[128];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack256(int stackSize, void *topOfStack)
///
/// @brief Allocate 256 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack256(int stackSize, void *topOfStack) {
  char stack[256];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack512(int stackSize, void *topOfStack)
///
/// @brief Allocate 512 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack512(int stackSize, void *topOfStack) {
  char stack[512];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack1024(int stackSize, void *topOfStack)
///
/// @brief Allocate 1024 bytes for the current stack.
///
/// @return This function returns no value.
void coroutineAllocateStack1024(int stackSize, void *topOfStack) {
  char stack[1024];

  allocateNextStackChunk(stackSize, topOfStack);
}

/// void coroutineAllocateStack(int stackSize)
///
/// @brief Allocate space for the current stack to grow before creating the
/// initial stack frame for the next coroutine.
///
/// @return This function returns no value.
void coroutineAllocateStack(int stackSize) {
  if (stackSize >= 1024) {
    coroutineAllocateStack1024(stackSize, NULL);
  } else if (stackSize >= 512) {
    coroutineAllocateStack512(stackSize, NULL);
  } else if (stackSize >= 256) {
    coroutineAllocateStack256(stackSize, NULL);
  } else if (stackSize >= 128) {
    coroutineAllocateStack128(stackSize, NULL);
  } else if (stackSize >= 64) {
    coroutineAllocateStack64(stackSize, NULL);
  } else if (stackSize >= 32) {
    coroutineAllocateStack32(stackSize, NULL);
  }
}

/// @fn int coroutineTerminate(Coroutine *targetCoroutine, Comutex **mutexes)
///
/// @brief Kill a coroutine that's currently in progress.
///
/// @param targetCoroutine A pointer to the Coroutine to terminate.
/// @param mutextes A one-dimensional, NULL-terminated array of mutexes to check
///   and unlock if they're locked by the Coroutine.
///
/// @note A Coroutine can only be blocked on a single Cocondition and the
/// information for that Cocondition is contained in the Coroutine structure, so
/// there's no need to pass in Coconditions to check.
///
/// @return Returns coroutineSuccess on success, coroutineError on error.
int coroutineTerminate(Coroutine *targetCoroutine, Comutex **mutexes) {
  if (targetCoroutine == NULL) {
    return coroutineError;
  }

  if (targetCoroutine->state == COROUTINE_STATE_NOT_RUNNING) {
    // It's not possible to take any action on this coroutine.  This is not an
    // error condition because the desired state is achieved.
    return coroutineSuccess;
  }

  // Remove the target coroutine from the running stack if applicable.
  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack hasn't been setup yet.  Bail.
    return coroutineError;
  }

  Coroutine* prev = NULL;
  for (; running != NULL; running = running->nextInList) {
    if (running == targetCoroutine) {
      if (prev != NULL) {
        prev->nextInList = targetCoroutine->nextInList;
      } else {
        // The target coroutine is the top of the running stack.
        coroutinePopRunning();
      }
      break;
    }
    prev = running;
  }

  // Halt the coroutine.
  targetCoroutine->priv = NULL;
  targetCoroutine->state = COROUTINE_STATE_NOT_RUNNING;
  memcpy(&targetCoroutine->context,
    &targetCoroutine->resetContext, sizeof(jmp_buf));
  coroutinePushIdle(targetCoroutine);

  // Unlock any mutexes the coroutine had locked.
  if (mutexes != NULL) {
    for (int i = 0; mutexes[i] != NULL; i++) {
      if (atomic_load(&mutexes[i]->coroutine) == targetCoroutine) {
        // Unlock the mutex.
        mutexes[i]->recursionLevel = 0;
        atomic_store(&mutexes[i]->coroutine, (Coroutine*) NULL);
      }
    }
  }

  // Remove the coroutine from any condition it was waiting on.
  Cocondition *cond = targetCoroutine->blockingCocondition;
  if (cond != NULL) {
    Coroutine **cur = &cond->head;
    while ((*cur != NULL) && (*cur != targetCoroutine)) {
      cur = &((*cur)->nextToSignal);
    }
    *cur = targetCoroutine->nextToSignal;
    if (cond->head == NULL) {
      // Empty queue.
      cond->tail = NULL;
    }
    if (cond->tail == targetCoroutine) {
      cond->tail = targetCoroutine->prevToSignal;
    }
    cond->numWaiters--;
    if (cond->numSignals > 0) {
      cond->numSignals--;
    }
  }
  // targetCoroutine->prevToSignal->nextToSignal is taken care of above.
  if (targetCoroutine->nextToSignal != NULL) {
    targetCoroutine->nextToSignal->prevToSignal = targetCoroutine->prevToSignal;
  }
  if (targetCoroutine->prevToSignal != NULL) {
    targetCoroutine->prevToSignal->nextToSignal = targetCoroutine->nextToSignal;
  }
  targetCoroutine->nextToSignal = NULL;
  targetCoroutine->prevToSignal = NULL;
  targetCoroutine->blockingCocondition = NULL;

  Comutex *mtx = targetCoroutine->blockingComutex;
  if (mtx != NULL) {
    Coroutine **cur = &mtx->head;
    while ((*cur != NULL) && (*cur != targetCoroutine)) {
      cur = &((*cur)->nextToLock);
    }
    *cur = targetCoroutine->prevToLock;
  }
  // targetCoroutine->prevToLock->nextToLock is taken care of above.
  if (targetCoroutine->nextToLock != NULL) {
    targetCoroutine->nextToLock->prevToLock = targetCoroutine->prevToLock;
  }
  if (targetCoroutine->prevToLock != NULL) {
    targetCoroutine->prevToLock->nextToLock = targetCoroutine->nextToLock;
  }
  targetCoroutine->nextToLock = NULL;
  targetCoroutine->prevToLock = NULL;
  targetCoroutine->blockingComutex = NULL;

  // Destroy any messages that were sent.
  // NOTE:  This must be done after we've taken care of the signals and mutexes
  // above because the coroutine may have been waiting on a message, in which
  // case its message queue mutex and condition will have been in use.
  comessageQueueDestroy(targetCoroutine);
  // Re-initialize the queue.
  comessageQueueCreate(targetCoroutine);

  return coroutineSuccess;
}

/// @fn int coroutineSetContext(Coroutine* coroutine, void *context)
///
/// @brief Set private context associated with a coroutine.
///
/// @param coroutine A pointer to the coroutine whose ID is to be set.  If this
///   value is NULL then the ID of the currently running coroutine will be set.
/// @param id An unsigned integer to set as the coroutine's ID.
///
/// @return This function always returns coroutineSuccess.
int coroutineSetContext(Coroutine* coroutine, void *context) {
  if (coroutine == NULL) {
    return coroutineError;
  }

  coroutine->priv = context;

  return coroutineSuccess;
}

/// @fn void* coroutineContext(Coroutine* coroutine)
///
/// @brief Get the private context associated with a coroutine.
///
/// @param coroutine A pointer to the coroutine of interest.  If this value is
///   NULL then NULL will be returned.
///
/// @return Returns the private context of the specified or current coroutine.
/// NULL will be returned if the private context of the coroutine has not been
/// previously set with a call to coroutineSetContext.
void* coroutineContext(Coroutine* coroutine) {
  if (coroutine == NULL) {
    return NULL;
  }

  return coroutine->priv;
}

/// @fn CoroutineState coroutineState(Coroutine* coroutine)
///
/// @brief Get the state of a specified coroutine.
///
/// @param coroutine The Coroutine to examine.
///
/// @return Returns the state of the coroutine on success, NOT_RUNNING if the
/// provided pointer is NULL.
CoroutineState coroutineState(Coroutine* coroutine) {
  CoroutineState state = COROUTINE_STATE_NOT_RUNNING;

  if (coroutine != NULL) {
    state = coroutine->state;
  }

  return state;
}

#ifdef THREAD_SAFE_COROUTINES

/// @fn void coroutineSetThreadingSupportEnabled(bool state)
///
/// @brief Get the current value of the file-local
/// _coroutineThreadingSupportEnabled state variable.  This enables or disables
/// threading support at runtime on systems that support threading.
///
/// @return This function returns no value.
void coroutineSetThreadingSupportEnabled(bool state) {
  _coroutineThreadingSupportEnabled = state;
}

/// @fn bool coroutineThreadingSupportEnabled()
///
/// @brief Get the current value of the file-local
/// _coroutineThreadingSupportEnabled state variable.
///
/// @return Returns the value of _coroutineThreadingSupportEnabled.
bool coroutineThreadingSupportEnabled() {
  return _coroutineThreadingSupportEnabled;
}

#endif // THREAD_SAFE_COROUTINES

/// @fn int coroutineConfig(Coroutine *first, CoroutineConfigOptions *options)
///
/// @brief Configure the global or thread-specific defaults for all coroutines
/// allocated by the current thread.
///
/// @param first A pointer to the root Coroutine to use.  This parameter is
///   mandatory.
/// @param options A pointer to a CoroutineConfigOptions structure that provides
///   optional settings for coroutines.  This parameter is optional and may be
///   NULL.  See the Doxygen for the structure in Coroutines.h for more details.
///
/// @return Returns coroutineSuccess on success, coroutineError on error.
int coroutineConfig(Coroutine *first, CoroutineConfigOptions *options) {
  if (first == NULL) {
    fprintf(stderr, "NULL first provided to coroutineConfig.\n");
    return coroutineError;
  }
  
  int stackSize = COROUTINE_DEFAULT_STACK_SIZE;
  if ((options != NULL)
    && (options->stackSize >= COROUTINE_STACK_CHUNK_SIZE)
  ) {
    stackSize = options->stackSize;
  }

  // Reset the idle and running stacks.
#ifdef THREAD_SAFE_COROUTINES
  if (!_coroutineThreadingSupportEnabled) {
    _globalIdle = NULL;
    _globalRunning = NULL;
  } else {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    tss_set(_tssIdle, NULL);
    tss_set(_tssRunning, NULL);
  }
#else
  _globalIdle = NULL;
  _globalRunning = NULL;
#endif // THREAD_SAFE_COROUTINES

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    if (tss_get(_tssFirst) != NULL) {
      // coroutineConfig was already called once.  Everything has already been
      // configured, so we just need to reset _tssFirst and _tssRunning.
      if (first != NULL) {
        int status = tss_set(_tssFirst, first);
        if (status != thrd_success) {
          fprintf(stderr,
            "Could not set _tssFirst to first in coroutineConfig.\n");
          return coroutineError;
        }
        status = tss_set(_tssRunning, first);
        if (status != thrd_success) {
          fprintf(stderr,
            "Could not set _tssRunning to first in coroutineConfig.\n");
          return coroutineError;
        }
      }
    } else if (!coroutineInitializeThreadMetadata(first)) {
      fprintf(stderr,
        "Could not initialize thread metadata in coroutineConfig.\n");
        return coroutineError;
    }

    tss_set(_tssStackSize, (void*) ((intptr_t) stackSize));
    if (options != NULL) {
      tss_set(_tssStateData, options->stateData);
      if (options->coroutineYieldCallback != NULL) {
        free(tss_get(_tssCoroutineYieldCallback));
        CoroutineYieldCallback *coroutineYieldCallbackPointer
          = (CoroutineYieldCallback*) malloc(sizeof(CoroutineYieldCallback));
        *coroutineYieldCallbackPointer = options->coroutineYieldCallback;
        tss_set(_tssCoroutineYieldCallback, coroutineYieldCallbackPointer);
      } else {
        tss_set(_tssCoroutineYieldCallback, NULL);
      }

      if (options->comutexUnlockCallback != NULL) {
        free(tss_get(_tssComutexUnlockCallback));
        ComutexUnlockCallback *comutexUnlockCallbackPointer
          = (ComutexUnlockCallback*) malloc(sizeof(ComutexUnlockCallback));
        *comutexUnlockCallbackPointer = options->comutexUnlockCallback;
        tss_set(_tssComutexUnlockCallback, comutexUnlockCallbackPointer);
      } else {
        tss_set(_tssComutexUnlockCallback, NULL);
      }

      if (options->coconditionSignalCallback != NULL) {
        free(tss_get(_tssCoconditionSignalCallback));
        CoconditionSignalCallback *coconditionSignalCallbackPointer
          = (CoconditionSignalCallback*)
            malloc(sizeof(CoconditionSignalCallback));
        *coconditionSignalCallbackPointer = options->coconditionSignalCallback;
        tss_set(_tssCoconditionSignalCallback,
          coconditionSignalCallbackPointer);
      } else {
        tss_set(_tssCoconditionSignalCallback, NULL);
      }
    } else {
      tss_set(_tssStateData, NULL);

      free(tss_get(_tssCoroutineYieldCallback));
      tss_set(_tssCoroutineYieldCallback, NULL);

      free(tss_get(_tssComutexUnlockCallback));
      tss_set(_tssComutexUnlockCallback, NULL);

      free(tss_get(_tssCoconditionSignalCallback));
      tss_set(_tssCoconditionSignalCallback, NULL);
    }
  }
#endif // THREAD_SAFE_COROUTINES

  memset(first, 0, sizeof(Coroutine));

  if (comessageQueueCreate(first) != coroutineSuccess) {
    fprintf(stderr,
      "Could not initialize message queue for first coroutine.\n");
    return coroutineError;
  }

  // This function is called from what will become the main coroutine (pointed
  // to by the first pointer), so by definition, it's running.  Mark it as
  // such.
  first->state = COROUTINE_STATE_RUNNING;

  _globalFirst = first;
  _globalRunning = first;

  _globalStackSize = stackSize;
  if (options != NULL) {
    _globalStateData = options->stateData;
    _globalCoroutineYieldCallback = options->coroutineYieldCallback;
    _globalComutexUnlockCallback = options->comutexUnlockCallback;
    _globalCoconditionSignalCallback = options->coconditionSignalCallback;
  } else {
    _globalStateData = NULL;
    _globalCoroutineYieldCallback = NULL;
    _globalComutexUnlockCallback = NULL;
    _globalCoconditionSignalCallback = NULL;
  }

  return coroutineSuccess;
}

/// @fn int comutexInit(Comutex* mtx, int type)
///
/// @brief Initialize a coroutine mutex.
///
/// @param mtx A pointer to the allocated coroutine mutex to initialize.
/// @param type A bitwise-ored integer of the type of mutex this is to be.
///
/// @return This function always returns coroutine success in the current
/// implementation.
int comutexInit(Comutex *mtx, int type) {
  int returnValue = coroutineSuccess;

  if (mtx != NULL) {
    mtx->lastYieldValue = NULL;
    mtx->type = type;
    atomic_store(&mtx->coroutine, (Coroutine*) NULL);
    mtx->recursionLevel = 0;
    mtx->head = NULL;
    mtx->timeoutTime = 0;
  } else {
    returnValue = coroutineError;
  }

  return returnValue;
}

/// @fn int comutexLock(Comutex* mtx)
///
/// @brief Lock a coroutine mutex.
///
/// @details This function blocks the current coroutine, yielding each time it
/// tries and fails to acquire the lock.  The special value COROUTINE_WAIT
/// will be yielded to the caller each time control is yielded.
///
/// @param mtx A pointer to the coroutine mutex to lock.
///
/// @return Returns coroutineSuccess when the lock is acquired, coroutineError
/// if the provided pointer is NULL.
int comutexLock(Comutex *mtx) {
  if (mtx == NULL) {
    // Cannot honor the request.
    return coroutineError;
  }

  // Clear the lastYieldValue before we do anything else.
  mtx->lastYieldValue = NULL;

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return coroutineError;
    }
  }
#endif

  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    return coroutineError;
  }

  // Push ourselves onto the queue.
  running->nextToLock = NULL;
  Coroutine *prev = NULL;
  Coroutine **cur = &mtx->head;
  while (*cur != NULL) {
    prev = *cur;
    cur = &((*cur)->nextToLock);
  }
  *cur = running;
  running->prevToLock = prev;

  running->blockingComutex = mtx;
  while (comutexTryLock(mtx) != coroutineSuccess) {
    mtx->lastYieldValue = coroutineYield(NULL, COROUTINE_STATE_WAIT);
  }
  running->blockingComutex = NULL;

  // Remove ourselves from the queue.
  prev = NULL;
  cur = &mtx->head;
  while (*cur != running) {
    prev = *cur;
    cur = &((*cur)->nextToLock);
  }
  *cur = running->nextToLock;
  if (running->nextToLock != NULL) {
    running->nextToLock->prevToLock = prev;
  }

  return coroutineSuccess;
}

/// @fn int comutexUnlock(Comutex* mtx)
///
/// @brief Unlock a previously-locked coroutine mutex.
///
/// @param mtx A pointer to the coroutine mutex to unlock.
///
/// @return Returns coroutineSuccess if the currently-running coroutine has the
/// lock, coroutineError otherwise.  If the currently-running coroutine has the
/// lock and the depth of the unlock calls matches the depth of the depth of the
/// lock calls (mtx->recursionLevel reaches 0 with this call), the mutex is
/// unlocked.
int comutexUnlock(Comutex *mtx) {
  int returnValue = coroutineSuccess;

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return coroutineError;
    }
  }
#endif

  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    return coroutineError;
  }

  if ((mtx != NULL) && (atomic_load(&mtx->coroutine) == running)) {
    mtx->recursionLevel--;
    if (mtx->recursionLevel == 0) {
      void *stateData = _globalStateData;
      ComutexUnlockCallback comutexUnlockCallback
        = _globalComutexUnlockCallback;
#ifdef THREAD_SAFE_COROUTINES
      if (_coroutineThreadingSupportEnabled) {
        // No need to call coroutineSetupThreadMetadata or
        // coroutineInitializeThreadMetadata this time since we did that above.
        stateData = tss_get(_tssStateData);
        ComutexUnlockCallback *possibleCallback
          = (ComutexUnlockCallback*) tss_get(_tssComutexUnlockCallback);
        if (possibleCallback != NULL) {
          comutexUnlockCallback = *possibleCallback;
        }
      }
#endif
      if (comutexUnlockCallback != NULL) {
        comutexUnlockCallback(stateData, mtx);
      }

      atomic_store(&mtx->coroutine, (Coroutine*) NULL);
    }
  } else {
    returnValue = coroutineError;
  }

  return returnValue;
}

/// @fn void comutexDestroy(Comutex* mtx)
///
/// @brief Destroy a previously-initialized coroutine mutex.
///
/// @param mtx A pointer to the mutex to destroy.
///
/// @return This function returns no value.
void comutexDestroy(Comutex *mtx) {
  if (mtx != NULL) {
    mtx->lastYieldValue = NULL;
    mtx->type = 0;
    atomic_store(&mtx->coroutine, (Coroutine*) NULL);
    mtx->recursionLevel = 0;
    mtx->head = NULL;
    mtx->timeoutTime = 0;
  }
}

/// @fn int comutexTimedLock(Comutex* mtx, const struct timespec* ts)
///
/// @brief Attempt to lock a coroutine mutex until the lock is acquired or a
/// specified time is reached, whichever comes first.
///
/// @param mtx A pointer to the mutex to destroy.
/// @param ts A pointer to a struct timespec instance that specifies the future
///   deadline for abandoning attempts to lock the mutex.
///
/// @return Returns coroutineSuccess if the lock is acquired before the timeout
/// is reached, coroutineTimeout if the timeout is reached before the lock is
/// acquired, and coroutineError if the coroutine mutex is not a timed mutex,
/// if the current system time could not be acquired, or if one of the provided
/// parameters is NULL.
int comutexTimedLock(Comutex *mtx, const struct timespec *ts) {
  if ((mtx == NULL) || (ts == NULL)) {
    // Cannot honor the request.
    return coroutineError;
  }
  mtx->timeoutTime = coroutineGetNanoseconds(ts);

  // Clear the lastYieldValue before we do anything else.
  mtx->lastYieldValue = NULL;

  if (!(mtx->type & comutexTimed)) {
    // This is not a timed mutex.  It does not support timeouts.  We fail.
    mtx->timeoutTime = 0;
    return coroutineError;
  }

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      mtx->timeoutTime = 0;
      return coroutineError;
    }
  }
#endif

  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    mtx->timeoutTime = 0;
    return coroutineError;
  }

  // Push ourselves onto the queue.
  running->nextToLock = NULL;
  Coroutine *prev = NULL;
  Coroutine **cur = &mtx->head;
  while (*cur != NULL) {
    prev = *cur;
    cur = &((*cur)->nextToLock);
  }
  *cur = running;
  running->prevToLock = prev;

  int returnValue = comutexTryLock(mtx);
  running->blockingComutex = mtx;
  while (returnValue != coroutineSuccess) {
    if (coroutineGetNanoseconds(NULL) > mtx->timeoutTime) {
      returnValue = coroutineTimedout;
      break;
    }
    mtx->lastYieldValue = coroutineYield(NULL, COROUTINE_STATE_TIMEDWAIT);
    returnValue = comutexTryLock(mtx);
  }
  mtx->timeoutTime = 0;
  running->blockingComutex = NULL;

  // Remove ourselves from the queue.
  prev = NULL;
  cur = &mtx->head;
  while (*cur != running) {
    prev = *cur;
    cur = &((*cur)->nextToLock);
  }
  *cur = running->nextToLock;
  if (running->nextToLock != NULL) {
    running->nextToLock->prevToLock = prev;
  }

  return returnValue;
}

/// @fn int comutexTryLock(Comutex* mtx)
///
/// @brief Make one attempt to lock a coroutine mutex.
///
/// @param mtx A pointer to the coroutine mutex to attempt to lock.
///
/// @return Returns coroutineSuccess if the mutex is unlocked or if the current
/// coroutine has the lock and the mutex is recursive, coroutineBusy if the
/// mutex is locked by another coroutine, and coroutineError under any other
/// conditions.
int comutexTryLock(Comutex *mtx) {
  if (mtx == NULL) {
    // Cannot honor the request.
    return coroutineError;
  }

  int returnValue = coroutineError;

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return coroutineError;
    }
  }
#endif

  Coroutine *running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    return coroutineError;
  } else if ((mtx->head != NULL) && (mtx->head != running)) {
    return coroutineBusy;
  }

  Coroutine *cur = NULL;
  atomic_compare_exchange_strong(&mtx->coroutine, &cur, running);
  if (cur == NULL) {
    mtx->recursionLevel = 1;
    returnValue = coroutineSuccess;
  } else if ((cur == running) && (mtx->type & comutexRecursive)) {
    mtx->recursionLevel++;
    returnValue = coroutineSuccess;
  } else if (cur != running) {
    returnValue = coroutineBusy;
  } // else any other situation is an error, which is the value of returnValue

  return returnValue;
}

/// @fn void* comutexLastYieldValue(Comutex* mtx)
///
/// @brief Get the last value returned by a yield call in a blocking comutex
/// lock function.
///
/// @param mtx A pointer to the comutex to interrogate.
///
/// @return Returns the last value returned by a blocking comutex lock function
/// performed on the specified if there is such a value.  NULL if the lock
/// function succeeded without yielding or if the provided comutex is NULL.
void* comutexLastYieldValue(Comutex* mtx) {
  void *returnValue = NULL;

  if (mtx != NULL) {
    returnValue = mtx->lastYieldValue;
  }

  return returnValue;
}

/// @fn int coconditionBroadcast(Cocondition *cond)
///
/// @brief Broadcast a condition to all coroutines blocked on it.
///
/// @param cond A pointer to the coroutine condition to broadcast.
///
/// @return Returns coroutineSuccess on success, coroutineError if the call
/// could not be honored (cond is NULL).
int coconditionBroadcast(Cocondition *cond) {
  int returnValue = coroutineSuccess;

  if (cond != NULL) {
    cond->numSignals = cond->numWaiters;

    void *stateData = _globalStateData;
    CoconditionSignalCallback coconditionSignalCallback
      = _globalCoconditionSignalCallback;
#ifdef THREAD_SAFE_COROUTINES
    if (_coroutineThreadingSupportEnabled) {
      call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
      if (coroutineInitializeThreadMetadata(NULL)) {
        stateData = tss_get(_tssStateData);
        CoconditionSignalCallback *possibleCallback
          = (CoconditionSignalCallback*) tss_get(_tssCoconditionSignalCallback);
        if (possibleCallback != NULL) {
          coconditionSignalCallback = *possibleCallback;
        }
      }
    }
#endif
    if (coconditionSignalCallback != NULL) {
      coconditionSignalCallback(stateData, cond);
    }
  } else {
    returnValue = coroutineError;
  }

  return returnValue;
}

/// @fn void coconditionDestroy(Cocondition* cond)
///
/// @brief Destroy a previously initialized coroutine condition.
///
/// @param cond A pointer to the coroutine condition to destroy.
///
/// @return This function returns no value.
void coconditionDestroy(Cocondition *cond) {
  if (cond != NULL) {
    cond->lastYieldValue = NULL;
    cond->numWaiters = 0;
    cond->numSignals = -1;
    cond->head = NULL;
    cond->tail = NULL;
    cond->timeoutTime = 0;
  }
}

/// @fn int coconditionInit(Cocondition* cond)
///
/// @brief Initialize a coroutine condition variable.
///
/// @param cond A pointer to the coroutine condition to destroy.
///
/// @return Returns coroutineSuccess on successful initialization,
/// coroutineError if the request could not be honored (cond is NULL).
int coconditionInit(Cocondition* cond) {
  int returnValue = coroutineSuccess;

  if (cond != NULL) {
    cond->lastYieldValue = NULL;
    cond->numWaiters = 0;
    cond->numSignals = 0;
    cond->head = NULL;
    cond->tail = NULL;
    cond->timeoutTime = 0;
  } else {
    returnValue = coroutineError;
  }

  return returnValue;
}

/// @fn int coconditionSignal(Cocondition *cond)
///
/// @brief Signal a single coroutine blocked on a condition.
///
/// @param cond A pointer to the coroutine condition to signal.
///
/// @return Returns coroutineSuccess on successful signalling, coroutineError
/// if the call could not be honored (cond is NULL).
int coconditionSignal(Cocondition *cond) {
  int returnValue = coroutineSuccess;

  if (cond != NULL) {
    if (cond->numWaiters > 0) {
      cond->numSignals++;

      void *stateData = _globalStateData;
      CoconditionSignalCallback coconditionSignalCallback
        = _globalCoconditionSignalCallback;
#ifdef THREAD_SAFE_COROUTINES
      if (_coroutineThreadingSupportEnabled) {
        call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
        if (coroutineInitializeThreadMetadata(NULL)) {
          stateData = tss_get(_tssStateData);
          CoconditionSignalCallback *possibleCallback
            = (CoconditionSignalCallback*) tss_get(_tssCoconditionSignalCallback);
          if (possibleCallback != NULL) {
            coconditionSignalCallback = *possibleCallback;
          }
        }
      }
#endif
      if (coconditionSignalCallback != NULL) {
        coconditionSignalCallback(stateData, cond);
      }
    }
  } else {
    returnValue = coroutineError;
  }

  return returnValue;
}

/// @fn int coconditionTimedWait(Cocondition* cond, Comutex* mtx, const struct timespec* ts)
///
/// @brief Wait for a condition to be signalled or until a specified time,
/// whichever comes first.
///
/// @param cond A pointer to the condition to wait on.
/// @param mtx A mutex for the condition that must be locked before this call
///   is made.  It will be unlocked before blocking on the condition and locked
///   again before the function returns.
/// @param ts A struct timespec that specifies the future deadline of the wait.
///
/// @return Returns coroutineSuccess on success, coroutineTimedout if the
/// deadline is reached before the condition is signalled, or coroutineError
/// if the request could not be honored (a parameter is NULL or timespec_get
/// fails).
int coconditionTimedWait(Cocondition *cond, Comutex *mtx,
  const struct timespec *ts
) {
  if ((cond == NULL) || (mtx == NULL) || (ts == NULL)) {
    // Cannot honor the request.
    return coroutineError;
  }
  cond->timeoutTime = coroutineGetNanoseconds(ts);

  // Clear the lastYieldValue before we do anything else.
  cond->lastYieldValue = NULL;

  comutexUnlock(mtx);

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return coroutineError;
    }
  }
#endif

  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    cond->timeoutTime = 0;
    return coroutineError;
  }

  // Add ourselves to the queue.
  cond->numWaiters++;
  if (cond->tail != NULL) {
    cond->tail->nextToSignal = running;
  }
  running->prevToSignal = cond->tail;
  cond->tail = running;
  if (cond->head == NULL) {
    cond->head = running;
  }

  int returnValue = coroutineSuccess;
  running->blockingCocondition = cond;
  while ((cond->numSignals == 0) || (cond->head != running)) {
    cond->lastYieldValue = coroutineYield(NULL, COROUTINE_STATE_TIMEDWAIT);

    if (((cond->numSignals == 0) || (cond->head != running))
      && (coroutineGetNanoseconds(NULL) > cond->timeoutTime)
    ) {
      returnValue = coroutineTimedout;
      break;
    }
  }
  cond->timeoutTime = 0;
  running->blockingCocondition = NULL;
  if ((returnValue == coroutineSuccess) && (cond->numSignals > 0)) {
    // We are at the head of the queue.
    cond->numSignals--;
    cond->numWaiters--;
    cond->head = running->nextToSignal;
    if (running->prevToSignal != NULL) {
      running->prevToSignal->nextToSignal = running->nextToSignal;
    }
    if (running->nextToSignal != NULL) {
      running->nextToSignal->prevToSignal = running->prevToSignal;
    }
    if (cond->tail == running) {
      // We are the tail of the queue;
      cond->tail = running->prevToSignal;
    }
    returnValue = coroutineSuccess;
  } else if (returnValue == coroutineTimedout) {
    // Remove ourselves from the queue.  We could be anywhere in the queue, so
    // manage the links accordingly.
    if (running->prevToSignal != NULL) {
      running->prevToSignal->nextToSignal = running->nextToSignal;
    }
    if (running->nextToSignal != NULL) {
      running->nextToSignal->prevToSignal = running->prevToSignal;
    }
    if (cond->head == running) {
      cond->head = running->nextToSignal;
    }
    if (cond->tail == running) {
      // We are the tail of the queue;
      cond->tail = running->prevToSignal;
    }
    cond->numWaiters--;
  } else {
    // The condition has been destroyed out from under us.  Invalid state.
    returnValue = coroutineError;
  }
  running->nextToSignal = NULL;
  running->prevToSignal = NULL;

  comutexLock(mtx);
  return returnValue;
}

/// @fn int coconditionWait(Cocondition* cond, Comutex* mtx)
///
/// @brief Wait for the specified condition to be signalled.
///
/// @param cond A pointer to the condition to wait on.
/// @param mtx A mutex for the condition that must be locked before this call
///   is made.  It will be unlocked before blocking on the condition and locked
///   again before the function returns.
///
/// @return Returns coroutineSuccess on success or coroutineError if the request
/// could not be honored (one or more NULL parameters).
int coconditionWait(Cocondition *cond, Comutex *mtx) {
  if ((cond == NULL) || (mtx == NULL)) {
    // Cannot honor the request.
    return coroutineError;
  }

  // Clear the lastYieldValue before we do anything else.
  cond->lastYieldValue = NULL;

  comutexUnlock(mtx);

#ifdef THREAD_SAFE_COROUTINES
  if (_coroutineThreadingSupportEnabled) {
    call_once(&_threadMetadataSetup, coroutineSetupThreadMetadata);
    if (!coroutineInitializeThreadMetadata(NULL)) {
      return coroutineError;
    }
  }
#endif

  Coroutine* running = getRunningCoroutine();
  if (running == NULL) {
    // running stack not setup yet.  Bail.
    return coroutineError;
  }

  // Add ourselves to the queue.
  cond->numWaiters++;
  if (cond->tail != NULL) {
    cond->tail->nextToSignal = running;
  }
  running->prevToSignal = cond->tail;
  cond->tail = running;
  if (cond->head == NULL) {
    cond->head = running;
  }

  int returnValue = coroutineSuccess;
  running->blockingCocondition = cond;
  while ((cond->numSignals == 0) || (cond->head != running)) {
    cond->lastYieldValue = coroutineYield(NULL, COROUTINE_STATE_WAIT);
  }
  running->blockingCocondition = NULL;
  if (cond->numSignals > 0) {
    cond->numSignals--;
    cond->numWaiters--;
    cond->head = running->nextToSignal;
    if (running->prevToSignal != NULL) {
      running->prevToSignal->nextToSignal = running->nextToSignal;
    }
    if (running->nextToSignal != NULL) {
      running->nextToSignal->prevToSignal = running->prevToSignal;
    }
    if (cond->tail == running) {
      // We are the tail of the queue;
      cond->tail = running->prevToSignal;
    }
  } else {
    // The condition has been destroyed out from under us.  Invalid state.
    returnValue = coroutineError;
  }
  running->nextToSignal = NULL;
  running->prevToSignal = NULL;

  comutexLock(mtx);
  return returnValue;
}

/// @fn void* coconditionLastYieldValue(Cocondition* cond)
///
/// @brief Retrieve the last value yielded to a condition wait call.
///
/// @param cond A pointer to the condition to interrogate.
///
/// @returns The last value yielded on the condition on success, NULL if the
/// provided condition pointer is NULL.
void* coconditionLastYieldValue(Cocondition* cond) {
  void *returnValue = NULL;

  if (cond != NULL) {
    returnValue = cond->lastYieldValue;
  }

  return returnValue;
}

/// @fn bool coroutineDeadlocked(Coroutine *coroutine)
///
/// @brief Determine whether or not a coroutine is deadlocked.
///
/// @param coroutine A pointer to the coroutine to examine.
///
/// @return Returns true if deadlock is detected, false if not.
bool coroutineDeadlocked(Coroutine *coroutine) {
  Coroutine *initialCoroutine = coroutine;
  bool returnValue = false;

  while ((coroutineState(coroutine) == COROUTINE_STATE_WAIT)
    && (coroutine->blockingComutex != NULL)
  ) {
    coroutine = atomic_load(&coroutine->blockingComutex->coroutine);
    if (coroutine == initialCoroutine) {
      returnValue = true;
      break;
    }
  }

  return returnValue;
}

/// @fn int comessageQueueCreate(Coroutine *coroutine)
///
/// @brief Initialize the message queue for the specified coroutine.
///
/// @param coroutine A pointer to the coroutine to initialize the queue of.
///
/// @return Returns coroutineSuccess on success, coroutineError on failure.
int comessageQueueCreate(Coroutine *coroutine) {
  int returnValue = coroutineError;

  if (coroutine == NULL) {
    return returnValue; // coroutineError
  }

  if (msg_q_create(&coroutine->messageQueue, MSG_CORO_SAFE)
    == &coroutine->messageQueue
  ) {
    returnValue = coroutineSuccess;
  }

  return returnValue;
}

/// @fn int comessageQueueDestroy(Coroutine *coroutine)
///
/// @brief Destroy the message queue for the specified coroutine.
///
/// @param coroutine A pointer to the Coroutine to destroy the queue of.
///
/// @return Returns coroutineSuccess on success, coroutineError on failure.
int comessageQueueDestroy(Coroutine *coroutine) {
  int returnValue = coroutineSuccess;

  if (coroutine == NULL) {
    // Nothing to do.  The queue for a NULL coroutine is already destroyed, so
    // this call is successful.
    return returnValue; // coroutineSuccess
  }

  return msg_q_destroy(&coroutine->messageQueue);
}

/// @fn msg_t* comessageQueuePeek(void)
///
/// @brief Get the head of the running coroutine's message queue but do not
/// remove it from the queue.
///
/// @return Returns the head of the running coroutine's message queue on
/// success, NULL on failure.
msg_t* comessageQueuePeek(void) {
  msg_t *comessage = NULL;

  Coroutine *coroutine = getRunningCoroutine();
  if (coroutine != NULL) {
    comessage = msg_q_peek(&coroutine->messageQueue);
  }

  return comessage;
}

/// @fn msg_t* comessageQueuePop(void)
///
/// @brief Get the head of the running coroutine's message queue and remove it
/// from the queue.
///
/// @return Returns the head of the running coroutine's message queue on
/// success, NULL on failure.
msg_t* comessageQueuePop(void) {
  msg_t *head = NULL;

  Coroutine *coroutine = getRunningCoroutine();
  if (coroutine != NULL) {
    head = msg_q_pop(&coroutine->messageQueue);
  }

  return head;
}

/// @fn msg_t* comessageQueuePopType(int type)
///
/// @brief Get the first message of the specified type from the running
/// coroutine's message queue and remove it from the queue.
///
/// @param type The type of message to get.
///
/// @return Returns the first message of the specified type on success, NULL on
/// failure.
msg_t* comessageQueuePopType(int type) {
  msg_t *returnValue = NULL;

  Coroutine *coroutine = getRunningCoroutine();
  if (coroutine == NULL) {
    // Coroutines haven't been configured yet.
    return returnValue;
  }

  returnValue = msg_q_pop_type(&coroutine->messageQueue, type);

  return returnValue;
}

// Underlying worker function in Messages.c.  Expose the prototype here so that
// we can bypass a layer of abstraction into the Messages library.
msg_t* msg_q_wait_for_type_(msg_q_t *queue, int64_t *type,
  const struct timespec *ts);

/// @fn msg_t* comessageQueueWait(const struct timespec *ts)
///
/// @brief Wait for a message to be available in the current coroutine's message
/// queue.
///
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this parameter is NULL then an infinite timeout
///   will be used.
///
/// @return Returns the first message of the provided type if one is available
/// before the specified time.  Returns NULL if no such message is available
/// within that time period or if an error occurs.
msg_t* comessageQueueWait(const struct timespec *ts) {
  msg_t *returnValue = NULL;

  Coroutine *coroutine = getRunningCoroutine();
  if (coroutine != NULL) {
    returnValue = msg_q_wait_for_type_(&coroutine->messageQueue, NULL, ts);
  }

  return returnValue;
}

/// @fn msg_t* comessageQueueWaitForType(int64_t type, const struct timespec *ts)
///
/// @brief Wait for a message of a specified type to be available in the current
/// coroutine's message queue.
///
/// @param type The message type to look for.
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this parameter is NULL then an infinite timeout
///   will be used.
///
/// @return Returns the first message of the provided type if one is available
/// before the specified time.  Returns NULL if no such message is available
/// within that time period or if an error occurs.
msg_t* comessageQueueWaitForType(int64_t type, const struct timespec *ts) {
  msg_t *returnValue = NULL;

  Coroutine *coroutine = getRunningCoroutine();
  if (coroutine != NULL) {
    returnValue = msg_q_wait_for_type_(&coroutine->messageQueue, &type, ts);
  }

  return returnValue;
}

/// @fn int comessageQueuePush(Coroutine *coroutine, msg_t *msg)
///
/// @brief Push a message onto a coroutine's message queue.
///
/// @param coroutine A pointer to the Coroutine with the message queue to add
///   to.
///
/// @return Returns coroutineSuccess, coroutineError on failure.
int comessageQueuePush(Coroutine *coroutine, msg_t *msg) {
  int returnValue = coroutineError;

  if ((coroutine == NULL) || (coroutine->state == COROUTINE_STATE_NOT_RUNNING)
    || (msg == NULL)
  ) {
    // This is invalid.
    return returnValue; // coroutineError
  }

  msg->from.coro = getRunningCoroutine();
  msg->to.coro = coroutine;
  Coroutine *replyToCoroutine = getRunningCoroutine();
  msg_q_t *replyTo = NULL;
  if (replyToCoroutine != NULL) {
    replyTo = &replyToCoroutine->messageQueue;
  }

  return msg_q_push(&coroutine->messageQueue, replyTo, msg);
}

