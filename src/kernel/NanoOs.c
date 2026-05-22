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

// Doxygen marker
/// @file

// NanoOs includes
#include "Console.h"
#include "Hal.h"
#include "NanoOs.h"
#include "Processes.h"
#include "Scheduler.h"
#include "../user/NanoOsApi.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Externs
extern const User users[];
extern const int NUM_USERS;

// Thread callbacks.  ***DO NOT** do parameter validation.  These callbacks
// are set when threadConfig is called.  If these callbacks are called at
// all (which they should be), then we should assume that things are configured
// correctly.  This is in kernel space code, which we have full control over,
// so we should assume that things are setup correctly.  If they're not setup
// correctly, we should fix the configuration, not do parameter validation.
// These callbacks - especially yieldCallback - are in the critical
// path.  Single cycles matter.  Don't waste more time than we need to.

/// @fn void defaultSignalHandler(int signum)
///
/// @brief sighandler_t-compliant default signal handler for when a signal
/// callback is processed by resumeCallback.
///
/// @param signum The integer signal number that was sent.
///
/// @return This function returns no value.
void defaultSignalHandler(int signum) {
  (void) signum;
}

/// @fn void* resumeCallback(void *arg)
///
/// @brief Callback to be called when a thread is resumed with a non-NULL
/// argument.
///
/// @param arg The argument that the thread is resumed with.
///
/// @return Returns a pointer to whatever value was processed by the appropriate
/// callback on successful parsing, the provied arg if nothing matched.
void* resumeCallback(void *arg) {
  void *returnValue = arg;
  uint64_t *signature = (uint64_t*) arg;

  switch (*signature) {
    case SIGNAL_SIGNATURE:
      {
        SignalCallback *signalCallback = (SignalCallback*) arg;
        defaultSignalHandler(signalCallback->signum);
        returnValue = NULL;
        break;
      }
  }

  return returnValue;
}

/// @fn void yieldCallback(void *stateData, Thread *thread)
///
/// @brief Function to be called right before a thread yields.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param thread A pointer to the Thread structure representing the
///   thread that's about to yield.  This parameter is unused by this
///   function.
///
/// @Return This function returns no value.
void yieldCallback(void *stateData, Thread *thread) {
  (void) thread;
  SchedulerState *schedulerState = *((SchedulerState**) stateData);
  if (schedulerState == NULL) {
    // We're being called before the scheduler has been started.  This is
    // sometimes done to fix the stack size of the scheduler itself before
    // starting it.  Just return.
    return;
  }

  // No need to check HAL->timer for NULL.  This function can't be configured
  // to be called unless it wasn't NULL at boot.
  HAL->timer->cancel(schedulerState->preemptionTimer);

  return;
}

/// @fn void unlockCallback(void *stateData, Comutex *comutex)
///
/// @brief Function to be called when a mutex (Comutex) is unlocked.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param comutex A pointer to the Comutex object that has been unlocked.  At
///   the time this callback is called, the mutex has been unlocked but its
///   thread pointer has not been cleared.
///
/// @return This function returns no value, but if the head of the Comutex's
/// lock queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void unlockCallback(void *stateData, Comutex *comutex) {
  (void) stateData;
  ProcessDescriptor *processDescriptor = threadContext(comutex->head);
  if (processDescriptor == NULL) {
    // Nothing is waiting on this mutex.  Just return.
    return;
  }
  processQueueRemove(processDescriptor->processQueue, processDescriptor);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);

  return;
}

/// @fn void signalCallback(
///   void *stateData, Cocondition *cocondition)
///
/// @brief Function to be called when a condition (Cocondition) is signalled.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param cocondition A pointer to the Cocondition object that has been
///   signalled.  At the time this callback is called, the number of signals has
///   been set to the number of waiters that will be signalled.
///
/// @return This function returns no value, but if the head of the Cocondition's
/// signal queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void signalCallback(void *stateData, Cocondition *cocondition) {
  (void) stateData;
  Thread *cur = cocondition->head;

  for (int ii = 0; (ii < cocondition->numSignals) && (cur != NULL); ii++) {
    ProcessDescriptor *processDescriptor = threadContext(cur);
    // It's not possible for processDescriptor to be NULL.  We only enter this
    // loop if cocondition->numSignals > 0, so there MUST be something waiting
    // on this condition.
    processQueueRemove(processDescriptor->processQueue, processDescriptor);
    processQueuePush(processDescriptor->readyQueue, processDescriptor);
    cur = cur->nextToSignal;
  }

  return;
}

/// @fn void nanoOsStart(void)
///
/// @brief Main entrypoint for the OS that is to be called from whatever the
/// bootloader's entrypoint is.
///
/// @return This function returns no value and never returns.
void nanoOsStart(void) {
  // Set the HAL pointer in the userspace API.
  nanoOsApi.hal = HAL;

  // SchedulerState pointer that we will have to populate in startScheduler.
  SchedulerState *threadStatePointer = NULL;

  // We want the address of the first thread to be as close to the base as
  // possible.  Because of that, we need to create the first one before we enter
  // the scheduler.  That means we need to allocate the main thread here,
  // configure it, and then create and run one before we ever enter the
  // scheduler.
  Thread _mainThread;
  schedulerThread = &_mainThread;
  ThreadsConfigOptions threadsConfigOptions = {
    .stackSize = HAL->memory->processStackSize(USE_HAL_MEMORY_DEBUG),
    .stateData = &threadStatePointer,
    .resumeCallback = resumeCallback,
    .yieldCallback = NULL,
    .unlockCallback = unlockCallback,
    .signalCallback = signalCallback,
  };
  if ((HAL->timer != NULL) && (HAL->timer->numSupported > 0)) {
    threadsConfigOptions.yieldCallback = yieldCallback;
  }
  if (threadsConfig(&_mainThread, &threadsConfigOptions) != processSuccess) {
    printChar('t');
    printChar('h');
    printChar('r');
    printChar('e');
    printChar('a');
    printChar('d');
    printChar('s');
    printChar('C');
    printChar('o');
    printChar('n');
    printChar('f');
    printChar('i');
    printChar('g');
    printChar(' ');
    printChar('f');
    printChar('a');
    printChar('i');
    printChar('l');
    printChar('e');
    printChar('d');
    printChar('\n');
    while(1);
  }
  // Create but *DO NOT* resume one dummy process.  This will set the size of
  // the main stack.
  if (threadProvision(NULL, dummyProcess, NULL) == NULL) {
    printString("Could not set scheduler process's stack size.\n");
  }

  printDebugString("Extending scheduler stack.\n");
  for (uint8_t ii = 0;
    ii < HAL->memory->numExtraSchedulerStacks(USE_HAL_MEMORY_DEBUG);
    ii++
  ) {
    if (threadProvision(NULL, dummyProcess, NULL) == NULL) {
      printString("Could not increase scheduler process's stack size.\n");
    }
  }

  // Enter the scheduler.  This never returns.
  printDebugString("Starting scheduler.\n");
  startScheduler(&threadStatePointer);
}
/// @fn Pid getNumPipes(const char *commandLine)
///
/// @brief Get the number of pipes in a commandLine.
///
/// @param commandLine The command line as read in from a console port.
///
/// @return Returns the number of pipe characters found in the command line.
Pid getNumPipes(const char *commandLine) {
  Pid numPipes = 0;
  const char *pipeAt = NULL;

  do {
    pipeAt = strchr(commandLine, '|');
    if (pipeAt != NULL) {
      numPipes++;
      commandLine = pipeAt + 1;
    }
  } while (pipeAt != NULL);

  return numPipes;
}

/// @var processStorage
///
/// @brief File-local variable to hold the per-process storage.
static void *processStorage[NANO_OS_NUM_PROCESSES][NUM_PROCESS_STORAGE_KEYS] = {0};

/// @fn void *getProcessStorage(uint8_t key)
///
/// @brief Get a previously-set value from per-process storage.
///
/// @param key The index into the process's per-process storage to retrieve.
///
/// @return Returns the previously-set value on success, NULL on failure.
void *getProcessStorage(uint8_t key) {
  void *returnValue = NULL;
  if (key >= NUM_PROCESS_STORAGE_KEYS) {
    // Key is out of range.
    return returnValue; // NULL
  }

  int processIndex = ((int) getRunningPid()) - 1;
  if ((processIndex >= 0) && (processIndex < NANO_OS_NUM_PROCESSES)) {
    // Calling process is not supported and does not have storage.
    returnValue = processStorage[processIndex][key];
  }

  return returnValue;
}

/// @fn int setProcessStorage_(uint8_t key, void *val, int pid, ...)
///
/// @brief Set the value of a piece of per-process storage.
///
/// @param key The index into the process's per-process storage to retrieve.
/// @param val The pointer value to set for the storage.
/// @param pid The ID of the process to set.  This value may only be set
///   by the scheduler.
///
/// @return Returns processSuccess on success, processError on failure.
int setProcessStorage_(uint8_t key, void *val, int pid, ...) {
  int returnValue = processError;
  if (key >= NUM_PROCESS_STORAGE_KEYS) {
    // Key is out of range.
    return returnValue; // processError
  }

  if (pid < 0) {
    if (getRunningPid() == SCHEDULER_STATE->schedulerPid) {
      pid = (int) getRunningPid();
    } else {
      return returnValue; // processError
    }
  }
  int processIndex = pid - 1;
  if ((processIndex >= 0) && (processIndex < NANO_OS_NUM_PROCESSES)) {
    // Calling process is not supported and does not have storage.
    processStorage[processIndex][key] = val;
    returnValue = processSuccess;
  }

  return returnValue;
}

/// @fn void timespecFromDelay(struct timespec *ts, long int delayMs)
///
/// @brief Initialize the value of a struct timespec with a time in the future
/// based upon the current time and a specified delay period.  The timespec
/// will hold the value of the current time plus the delay.
///
/// @param ts A pointer to a struct timespec to initialize.
/// @param delayMs The number of milliseconds in the future the timespec is to
///   be initialized with.
///
/// @return This function returns no value.
void timespecFromDelay(struct timespec *ts, long int delayMs) {
  if (ts == NULL) {
    // Bad data.  Do nothing.
    return;
  }

  timespec_get(ts, TIME_UTC);
  ts->tv_sec += (delayMs / 1000);
  ts->tv_nsec += (delayMs * 1000000);

  return;
}

/// @fn unsigned int raiseUInt(unsigned int x, unsigned int y)
///
/// @brief Raise a non-negative integer to a non-negative exponent.
///
/// @param x The base number to raise.
/// @param y The exponent to raise the base to.
///
/// @param Returns the result of x ** y.
unsigned int raiseUInt(unsigned int x, unsigned int y) {
  unsigned int z = 1;
  unsigned int multiplier = x;

  while (y > 0) {
    if (y & 1) {
      z *= multiplier;
    }

    multiplier *= multiplier;
    y >>= 1;
  }

  return z;
}

/// @fn const char* getUsernameByUserId(UserId userId)
///
/// @brief Get the username for a user given their numeric user ID.
///
/// @param userId The numeric ID of the user.
///
/// @return Returns the username of the user on success, NULL on failure.
const char* getUsernameByUserId(UserId userId) {
  const char *username = "unowned";

  for (int ii = 0; ii < NUM_USERS; ii++) {
    if (users[ii].userId == userId) {
      username = users[ii].username;
      break;
    }
  }

  return username;
}

/// @fn UserId getUserIdByUsername(const char *username)
///
/// @brief Get the numeric ID of a user given their username.
///
/// @param username The username string for the user.
///
/// @return Returns the numeric ID of the user on success, NO_USER_ID on
/// failure.
UserId getUserIdByUsername(const char *username) {
  UserId userId = NO_USER_ID;

  for (int ii = 0; ii < NUM_USERS; ii++) {
    if (strcmp(users[ii].username, username) == 0) {
      userId = users[ii].userId;
      break;
    }
  }

  return userId;
}

/// @var users
///
/// @brief The array of user information to simulate a user database.
const User users[] = {
  {
    .userId   = 0,
    .username = "root",
    .checksum = 1356, // rootroot
  },
  {
    .userId   = 1000,
    .username = "user1",
    .checksum = 1488, // user1user1
  },
  {
    .userId   = 1001,
    .username = "user2",
    .checksum = 1491, // user2user2
  },
};

/// @var NUM_USERS
///
/// @brief The number of users in the users array.
const int NUM_USERS = sizeof(users) / sizeof(users[0]);
