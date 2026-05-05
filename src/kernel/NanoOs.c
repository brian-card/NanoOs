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

// Custom includes
#include "Console.h"
#include "NanoOs.h"
#include "Processes.h"
#include "Scheduler.h"
#include "../user/NanoOsLibC.h"

#include "../user/NanoOsStdio.h"

// Externs
extern const User users[];
extern const int NUM_USERS;

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
