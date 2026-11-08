///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_fs_handles.c
///
/// @brief             Kernel tests for who may use an open file or directory.
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
#include "HalMock.h"

#include <string.h>

#include "kernel/Console.h"
#include "kernel/Filesystem.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsDebug.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSpawn.h"
#include "user/NanoOsStdio.h"
#include "user/NanoOsUnistd.h"

_Static_assert(EBADF == 13, "FILESYSTEM_EBADF must match EBADF");
_Static_assert(EMFILE == 29, "FILESYSTEM_EMFILE must match EMFILE");
_Static_assert(ENOMEM == 3, "FILESYSTEM_ENOMEM must match ENOMEM");

// These suite-local copies of the handlers run against stub drivers.  They
// pull in the host's <errno.h>, so the tests compare against the constants
// the handlers use, which the assertions above tie to NanoOs's.
#define NANO_OS_KERNEL_BUILD
#define driverFopen stubFopen
#define driverFclose stubFclose
#define driverFread stubFread
#define driverFwrite stubFwrite
#define driverFseek stubFseek
#define driverFeof stubFeof
#define driverOpendir stubOpendir
#define driverReaddir stubReaddir
#define driverClosedir stubClosedir
#define OpenFile testOpenFile
#define CloseFile testCloseFile
#define ReadFile testReadFile
#define WriteFile testWriteFile
#define SeekFile testSeekFile
#define EndOfFile testEndOfFile
#define Opendir testOpendir
#define Readdir testReaddir
#define Closedir testClosedir
#define CloseProcess testCloseProcess
#include "../../usr/src/filesystems/include/FilesystemPaths.h"
#include "../../usr/src/filesystems/common/OpenFileCommandHandler.c"
#include "../../usr/src/filesystems/common/CloseFileCommandHandler.c"
#include "../../usr/src/filesystems/common/ReadFileCommandHandler.c"
#include "../../usr/src/filesystems/common/WriteFileCommandHandler.c"
#include "../../usr/src/filesystems/common/SeekFileCommandHandler.c"
#include "../../usr/src/filesystems/common/EndOfFileCommandHandler.c"
#include "../../usr/src/filesystems/common/OpendirCommandHandler.c"
#include "../../usr/src/filesystems/common/ReaddirCommandHandler.c"
#include "../../usr/src/filesystems/common/ClosedirCommandHandler.c"
#include "../../usr/src/filesystems/common/CloseProcessCommandHandler.c"

#undef errno
#define errno (*errno_())

/// @var stubDirHandle
///
/// @brief What stubOpendir returns.
static int stubDirHandle;

/// @var stubFileHandle
///
/// @brief What stubFopen returns.
static int stubFileHandle;

/// @var stubEntry
///
/// @brief What stubReaddir returns.
static struct dirent stubEntry;

/// @var stubDirsClosed
///
/// @brief The number of directories the stub driver has closed.
static int stubDirsClosed;

/// @var stubFilesClosed
///
/// @brief The number of files the stub driver has closed.
static int stubFilesClosed;

/// @var stubIoCalls
///
/// @brief The number of reads and writes the stub driver has done.
static int stubIoCalls;

void* stubFopen(void *driverState, const char *filePath, const char *mode,
  time_t now, int *errorNumber
) {
  (void) driverState; (void) filePath; (void) mode; (void) now;
  *errorNumber = 0;
  return &stubFileHandle;
}

int stubFclose(void *driverState, void *fileHandle) {
  (void) driverState; (void) fileHandle;
  stubFilesClosed++;
  return 0;
}

int stubFread(void *driverState, void *ptr, uint32_t length, void *handle) {
  (void) driverState; (void) ptr; (void) handle;
  stubIoCalls++;
  return (int) length;
}

int stubFwrite(void *driverState, void *ptr, uint32_t length, void *handle) {
  (void) driverState; (void) ptr; (void) handle;
  stubIoCalls++;
  return (int) length;
}

long stubFseek(void *driverState, void *handle, long offset, int whence) {
  (void) driverState; (void) handle; (void) whence;
  return offset;
}

int stubFeof(void *handle) {
  (void) handle;
  return 0;
}

void* stubOpendir(void *driverState, const char *path, int *errorNumber) {
  (void) driverState; (void) path;
  *errorNumber = 0;
  return &stubDirHandle;
}

struct dirent* stubReaddir(void *driverState, void *handle, int *errorNumber) {
  (void) driverState;
  NANO_OS_ASSERT_EQ_PTR(&stubDirHandle, handle);
  *errorNumber = 0;
  return &stubEntry;
}

int stubClosedir(void *driverState, void *handle) {
  (void) driverState;
  NANO_OS_ASSERT_EQ_PTR(&stubDirHandle, handle);
  stubDirsClosed++;
  return 0;
}

/// @var handleState
///
/// @brief The FilesystemState the handlers run against.
static FilesystemState handleState;

/// @var handleDriverState
///
/// @brief A non-NULL driverState for handleState.
static int handleDriverState;

/// @var handleMessage
///
/// @brief The message the handlers are sent.
static ProcessMessage handleMessage;

/// @fn static void handleFixture(void)
///
/// @brief Start from an empty filesystem.
///
/// @return This function returns no value.
static void handleFixture(void) {
  memset(&handleState, 0, sizeof(handleState));
  handleState.driverState = &handleDriverState;
  handleState.args = &handleMessage;
  handleState.allProcesses = allProcesses;
  handleState.numProcesses = numProcesses;
  stubDirsClosed = 0;
  stubFilesClosed = 0;
  stubIoCalls = 0;
}

/// @fn static ProcessDescriptor* self(void)
///
/// @brief Get the running process, which owns what the tests open.
///
/// @return Returns the running process's ProcessDescriptor.
static ProcessDescriptor* self(void) {
  return getRunningProcess();
}

/// @fn static ProcessDescriptor* otherProcess(void)
///
/// @brief Get a process other than the running one.
///
/// @return Returns the console's ProcessDescriptor.
static ProcessDescriptor* otherProcess(void) {
  return &allProcesses[consolePid - 1];
}

/// @fn static void runHandler(void* (*handler)(void*),
///   ProcessDescriptor *sender, void *data)
///
/// @brief Run a command handler on a message stamped with a sender.
///
/// @param handler The handler.
/// @param sender The process the message is from.
/// @param data The command's arguments.
///
/// @return This function returns no value.
static void runHandler(void* (*handler)(void*), ProcessDescriptor *sender,
  void *data
) {
  memset(&handleMessage, 0, sizeof(handleMessage));
  processMessageInit(&handleMessage, 0, data, 0, false);
  handleMessage.from.coro = sender->mainThread;
  handler(&handleState);
  processMessageRelease(&handleMessage);
}

/// @fn static DIR* openDir(ProcessDescriptor *sender)
///
/// @brief Open a directory.
///
/// @param sender The process asking.
///
/// @return Returns the handle.
static DIR* openDir(ProcessDescriptor *sender) {
  static char path[] = "/tmp";
  FilesystemOpendirArgs args;
  memset(&args, 0, sizeof(args));
  args.pathname = path;
  runHandler(testOpendir, sender, &args);
  return args.returnValue;
}

/// @fn static int readDir(ProcessDescriptor *sender, DIR *dirp)
///
/// @brief Read a directory.
///
/// @param sender The process asking.
/// @param dirp The handle.
///
/// @return Returns 0 if an entry came back, otherwise the errno value.
static int readDir(ProcessDescriptor *sender, DIR *dirp) {
  FilesystemReaddirArgs args;
  memset(&args, 0, sizeof(args));
  args.dirp = dirp;
  runHandler(testReaddir, sender, &args);
  if (args.returnValue != NULL) {
    return 0;
  }
  return (args.errorNumber != 0) ? args.errorNumber : -1;
}

/// @fn static int closeDir(ProcessDescriptor *sender, DIR *dirp)
///
/// @brief Close a directory.
///
/// @param sender The process asking.
/// @param dirp The handle.
///
/// @return Returns what the handler returned.
static int closeDir(ProcessDescriptor *sender, DIR *dirp) {
  FilesystemClosedirArgs args;
  memset(&args, 0, sizeof(args));
  args.dirp = dirp;
  runHandler(testClosedir, sender, &args);
  return args.returnValue;
}

/// @fn static NanoOsFile* openFile(ProcessDescriptor *sender,
///   const char *mode)
///
/// @brief Open a file.
///
/// @param sender The process asking.
/// @param mode The fopen mode.
///
/// @return Returns the file.
static NanoOsFile* openFile(ProcessDescriptor *sender, const char *mode) {
  static char path[] = "/tmp/f";
  FilesystemFopenArgs args;
  memset(&args, 0, sizeof(args));
  args.pathname = path;
  args.mode = (char*) mode;
  runHandler(testOpenFile, sender, &args);
  NANO_OS_ASSERT_NOT_NULL(args.returnValue);
  return args.returnValue;
}

/// @fn static FilesystemIoCommandArgs doIo(void* (*handler)(void*),
///   ProcessDescriptor *sender, NanoOsFile *file)
///
/// @brief Read or write four bytes of a file.
///
/// @param handler testReadFile or testWriteFile.
/// @param sender The process asking.
/// @param file The file.
///
/// @return Returns the command's arguments as the handler left them.
static FilesystemIoCommandArgs doIo(void* (*handler)(void*),
  ProcessDescriptor *sender, NanoOsFile *file
) {
  static char buffer[4];
  FilesystemIoCommandArgs args;
  memset(&args, 0, sizeof(args));
  args.file = file;
  args.buffer = buffer;
  args.length = sizeof(buffer);
  runHandler(handler, sender, &args);
  return args;
}

/// @fn static void assertIoAllowed(void* (*handler)(void*), NanoOsFile *file)
///
/// @brief Assert that the running process can do an operation on a file.
///
/// @return This function returns no value.
static void assertIoAllowed(void* (*handler)(void*), NanoOsFile *file) {
  FilesystemIoCommandArgs args = doIo(handler, self(), file);
  NANO_OS_ASSERT_EQ_INT(4, (int) args.length);
  NANO_OS_ASSERT_EQ_INT(0, args.errorNumber);
}

/// @fn static void assertIoRefused(void* (*handler)(void*),
///   ProcessDescriptor *sender, NanoOsFile *file)
///
/// @brief Assert that an operation fails with EBADF and never reaches the
/// driver.
///
/// @return This function returns no value.
static void assertIoRefused(void* (*handler)(void*),
  ProcessDescriptor *sender, NanoOsFile *file
) {
  int ioCalls = stubIoCalls;
  FilesystemIoCommandArgs args = doIo(handler, sender, file);
  NANO_OS_ASSERT_EQ_INT(0, (int) args.length);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, args.errorNumber);
  NANO_OS_ASSERT_EQ_INT(ioCalls, stubIoCalls);
}

/// @fn static uint16_t dirGeneration(DIR *dirp)
///
/// @brief Get the generation a handle carries.
///
/// @return Returns the generation.
static uint16_t dirGeneration(DIR *dirp) {
  return (uint16_t) (((uintptr_t) dirp) >> FILESYSTEM_DIR_INDEX_BITS);
}

/// @fn static uint8_t dirIndex(DIR *dirp)
///
/// @brief Get the slot index a handle carries.
///
/// @return Returns the index.
static uint8_t dirIndex(DIR *dirp) {
  return (uint8_t) (((uintptr_t) dirp) & (FILESYSTEM_MAX_OPEN_DIRS - 1));
}

/// @fn static uint8_t numSlots(void)
///
/// @brief Get the size of the table.
///
/// @return Returns the number of slots.
static uint8_t numSlots(void) {
  return filesystemNumDirSlots(&handleState);
}

NANO_OS_KERNEL_TEST(fs_handles, access_follows_the_mode) {
  handleFixture();
  NanoOsFile *reader = openFile(self(), "r");
  assertIoAllowed(testReadFile, reader);
  assertIoRefused(testWriteFile, self(), reader);

  NanoOsFile *writer = openFile(self(), "w");
  assertIoAllowed(testWriteFile, writer);
  assertIoRefused(testReadFile, self(), writer);

  NanoOsFile *appender = openFile(self(), "a");
  assertIoAllowed(testWriteFile, appender);
  assertIoRefused(testReadFile, self(), appender);

  static const char *const updateModes[] = {"r+", "w+", "a+", "rb+"};
  for (size_t ii = 0; ii < 4; ii++) {
    NanoOsFile *file = openFile(self(), updateModes[ii]);
    assertIoAllowed(testReadFile, file);
    assertIoAllowed(testWriteFile, file);
  }
}

NANO_OS_KERNEL_TEST(fs_handles, another_process_cannot_use_a_file) {
  handleFixture();
  NanoOsFile *file = openFile(self(), "r+");
  assertIoRefused(testReadFile, otherProcess(), file);
  assertIoRefused(testWriteFile, otherProcess(), file);

  FilesystemSeekArgs seekArgs;
  memset(&seekArgs, 0, sizeof(seekArgs));
  seekArgs.stream = file;
  seekArgs.offset = 2;
  runHandler(testSeekFile, otherProcess(), &seekArgs);
  NANO_OS_ASSERT_EQ_INT(-1, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, seekArgs.errorNumber);
  NANO_OS_ASSERT_EQ_INT(0, (int) file->currentPosition);

  FeofArgs feofArgs;
  memset(&feofArgs, 0, sizeof(feofArgs));
  feofArgs.stream = file;
  runHandler(testEndOfFile, otherProcess(), &feofArgs);
  NANO_OS_ASSERT_EQ_INT(1, feofArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, feofArgs.errorNumber);

  FilesystemFcloseArgs closeArgs;
  memset(&closeArgs, 0, sizeof(closeArgs));
  closeArgs.stream = file;
  runHandler(testCloseFile, otherProcess(), &closeArgs);
  NANO_OS_ASSERT_EQ_INT(-FILESYSTEM_EBADF, closeArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(0, stubFilesClosed);
  NANO_OS_ASSERT_EQ_PTR(file, handleState.openFiles);
  NANO_OS_ASSERT_EQ_INT(1, handleState.numOpenFiles);
}

NANO_OS_KERNEL_TEST(fs_handles, the_owner_can_use_a_file) {
  handleFixture();
  NanoOsFile *file = openFile(self(), "r");
  NANO_OS_ASSERT_EQ_INT(self()->processId, file->owner);

  FilesystemSeekArgs seekArgs;
  memset(&seekArgs, 0, sizeof(seekArgs));
  seekArgs.stream = file;
  seekArgs.offset = 2;
  runHandler(testSeekFile, self(), &seekArgs);
  NANO_OS_ASSERT_EQ_INT(0, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(2, (int) file->currentPosition);

  FeofArgs feofArgs;
  memset(&feofArgs, 0, sizeof(feofArgs));
  feofArgs.stream = file;
  runHandler(testEndOfFile, self(), &feofArgs);
  NANO_OS_ASSERT_EQ_INT(0, feofArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(0, feofArgs.errorNumber);

  FilesystemFcloseArgs closeArgs;
  memset(&closeArgs, 0, sizeof(closeArgs));
  closeArgs.stream = file;
  runHandler(testCloseFile, self(), &closeArgs);
  NANO_OS_ASSERT_EQ_INT(0, closeArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(1, stubFilesClosed);
  NANO_OS_ASSERT_NULL(handleState.openFiles);
}

NANO_OS_KERNEL_TEST(fs_handles, a_null_file_is_refused) {
  handleFixture();
  assertIoRefused(testReadFile, self(), NULL);
  assertIoRefused(testWriteFile, self(), NULL);

  FilesystemFcloseArgs closeArgs;
  memset(&closeArgs, 0, sizeof(closeArgs));
  runHandler(testCloseFile, self(), &closeArgs);
  NANO_OS_ASSERT_EQ_INT(-FILESYSTEM_EBADF, closeArgs.returnValue);
}

NANO_OS_KERNEL_TEST(fs_handles, a_directory_handle_works_for_its_owner) {
  handleFixture();
  DIR *dirp = openDir(self());
  NANO_OS_ASSERT_NOT_NULL(dirp);
  NANO_OS_ASSERT_TRUE((uintptr_t) dirp <= UINT16_MAX);
  NANO_OS_ASSERT_EQ_INT(0, readDir(self(), dirp));
  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), dirp));
  NANO_OS_ASSERT_EQ_INT(1, stubDirsClosed);
}

NANO_OS_KERNEL_TEST(fs_handles, another_process_cannot_use_a_directory) {
  handleFixture();
  DIR *dirp = openDir(self());
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(otherProcess(), dirp));
  NANO_OS_ASSERT_EQ_INT(-1, closeDir(otherProcess(), dirp));
  NANO_OS_ASSERT_EQ_INT(0, stubDirsClosed);
  NANO_OS_ASSERT_EQ_INT(0, readDir(self(), dirp));
}

NANO_OS_KERNEL_TEST(fs_handles, a_closed_handle_fails_even_after_reuse) {
  handleFixture();
  DIR *stale = openDir(self());
  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), stale));
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(self(), stale));
  NANO_OS_ASSERT_EQ_INT(-1, closeDir(self(), stale));

  // Force the same slot to be reused.
  for (uint8_t ii = 0; ii < numSlots(); ii++) {
    if (ii != dirIndex(stale)) {
      handleState.openDirs[ii].owner = consolePid;
    }
  }
  DIR *fresh = openDir(self());
  NANO_OS_ASSERT_EQ_INT(dirIndex(stale), dirIndex(fresh));
  NANO_OS_ASSERT_TRUE(fresh != stale);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(self(), stale));
  NANO_OS_ASSERT_EQ_INT(0, readDir(self(), fresh));
}

NANO_OS_KERNEL_TEST(fs_handles,
  a_closed_slot_is_free_with_the_next_generation
) {
  handleFixture();
  DIR *dirp = openDir(self());
  FilesystemOpenDir *slot = &handleState.openDirs[dirIndex(dirp)];
  uint16_t generation = slot->generation;
  NANO_OS_ASSERT_EQ_INT(1, generation);
  NANO_OS_ASSERT_EQ_INT(generation, dirGeneration(dirp));

  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), dirp));
  NANO_OS_ASSERT_EQ_INT(0, slot->owner);
  NANO_OS_ASSERT_NULL(slot->driverHandle);
  NANO_OS_ASSERT_EQ_INT(generation + 1, slot->generation);
}

NANO_OS_KERNEL_TEST(fs_handles, a_wrapping_generation_skips_zero) {
  handleFixture();
  DIR *first = openDir(self());
  FilesystemOpenDir *slot = &handleState.openDirs[dirIndex(first)];
  slot->generation = FILESYSTEM_DIR_GENERATION_MASK;
  DIR *dirp = (DIR*) (((uintptr_t) slot->generation
      << FILESYSTEM_DIR_INDEX_BITS) | dirIndex(first));
  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), dirp));
  NANO_OS_ASSERT_EQ_INT(1, slot->generation);
}

NANO_OS_KERNEL_TEST(fs_handles, garbage_handles_are_refused) {
  handleFixture();
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF,
    readDir(self(), (DIR*) (uintptr_t) 0x1234));

  DIR *dirp = openDir(self());
  static const uintptr_t garbage[] = {
    0, 1, 0xffff, 0x10000, (uintptr_t) -1,
  };
  for (size_t ii = 0; ii < sizeof(garbage) / sizeof(garbage[0]); ii++) {
    NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF,
      readDir(self(), (DIR*) garbage[ii]));
    NANO_OS_ASSERT_EQ_INT(-1, closeDir(self(), (DIR*) garbage[ii]));
  }
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(self(),
    (DIR*) (((uintptr_t) dirp) + (((uintptr_t) 1) << 20))));
  NANO_OS_ASSERT_EQ_INT(0, readDir(self(), dirp));
}

NANO_OS_KERNEL_TEST(fs_handles, an_index_past_the_table_is_refused) {
  handleFixture();
  NANO_OS_ASSERT_TRUE(numSlots() > 3);
  NANO_OS_ASSERT_TRUE(consolePid <= 3);
  DIR *fourth = NULL;
  for (int ii = 0; ii < 4; ii++) {
    fourth = openDir(otherProcess());
  }
  NANO_OS_ASSERT_EQ_INT(3, dirIndex(fourth));

  // Three processes keep the console a valid sender but leave only slots 0-2.
  handleState.numProcesses = 3;
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(otherProcess(), fourth));
}

NANO_OS_KERNEL_TEST(fs_handles, a_full_table_returns_EMFILE) {
  handleFixture();
  uint8_t slots = numSlots();
  NANO_OS_ASSERT_TRUE(slots <= FILESYSTEM_MAX_OPEN_DIRS);
  for (uint8_t ii = 0; ii < slots; ii++) {
    NANO_OS_ASSERT_NOT_NULL(openDir(self()));
  }

  static char path[] = "/tmp";
  FilesystemOpendirArgs args;
  memset(&args, 0, sizeof(args));
  args.pathname = path;
  runHandler(testOpendir, self(), &args);
  NANO_OS_ASSERT_NULL(args.returnValue);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EMFILE, args.errorNumber);
}

NANO_OS_KERNEL_TEST(fs_handles, opendir_takes_the_lowest_generation) {
  handleFixture();
  (void) openDir(self());
  for (uint8_t ii = 0; ii < numSlots(); ii++) {
    handleState.openDirs[ii].owner = 0;
    handleState.openDirs[ii].generation = 50 + ii;
  }
  handleState.openDirs[3].generation = 7;
  handleState.openDirs[1].owner = consolePid;
  handleState.openDirs[1].generation = 2;

  DIR *dirp = openDir(self());
  NANO_OS_ASSERT_EQ_INT(3, dirIndex(dirp));
  NANO_OS_ASSERT_EQ_INT(7, dirGeneration(dirp));
}

NANO_OS_KERNEL_TEST(fs_handles, a_sender_outside_the_table_is_refused) {
  handleFixture();
  DIR *open = openDir(self());
  DIR *closed = openDir(self());
  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), closed));

  // A descriptor outside allProcesses that claims the owner's pid, and one
  // that claims pid 0 for the closed slot's owner of 0.
  ProcessDescriptor forged;
  memset(&forged, 0, sizeof(forged));
  forged.processId = self()->processId;
  ProcessDescriptor *carrier = otherProcess();
  void *context = coroutineContext(carrier->mainThread);

  // A handle carrying the closed slot's current generation, which a lookup
  // that treated the sender as pid 0 would match against the slot's owner.
  DIR *current = (DIR*) (((uintptr_t) closed)
    + (((uintptr_t) 1) << FILESYSTEM_DIR_INDEX_BITS));

  for (int ii = 0; ii < 3; ii++) {
    coroutineSetContext(carrier->mainThread,
      (ii == 2) ? NULL : (void*) &forged);
    forged.processId = (ii == 0) ? self()->processId : 0;
    int openStatus = readDir(carrier, open);
    int closedStatus = readDir(carrier, closed);
    int currentStatus = readDir(carrier, current);
    coroutineSetContext(carrier->mainThread, context);
    NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, openStatus);
    NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, closedStatus);
    NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, currentStatus);
  }
}

NANO_OS_KERNEL_TEST(fs_handles,
  a_sender_outside_the_table_cannot_open_a_slot
) {
  handleFixture();
  ProcessDescriptor forged;
  memset(&forged, 0, sizeof(forged));
  ProcessDescriptor *carrier = otherProcess();
  void *context = coroutineContext(carrier->mainThread);
  coroutineSetContext(carrier->mainThread, &forged);
  static char path[] = "/tmp";
  FilesystemOpendirArgs args;
  memset(&args, 0, sizeof(args));
  args.pathname = path;
  runHandler(testOpendir, carrier, &args);
  coroutineSetContext(carrier->mainThread, context);
  NANO_OS_ASSERT_NULL(args.returnValue);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, args.errorNumber);
}

/// @fn static void closeProcess(ProcessDescriptor *sender, ProcessId pid)
///
/// @brief Run FILESYSTEM_CLOSE_PROCESS.
///
/// @param sender The process asking.
/// @param pid The process that exited.
///
/// @return This function returns no value.
static void closeProcess(ProcessDescriptor *sender, ProcessId pid) {
  FilesystemCloseProcessArgs args = { .pid = pid };
  runHandler(testCloseProcess, sender, &args);
}

NANO_OS_KERNEL_TEST(fs_handles, closing_a_process_frees_only_what_it_owned) {
  handleFixture();
  DIR *mine = openDir(self());
  DIR *theirs = openDir(otherProcess());
  NanoOsFile *myFile = openFile(self(), "r");
  NanoOsFile *theirFile = openFile(otherProcess(), "r");
  (void) myFile;
  uint8_t theirIndex = dirIndex(theirs);
  uint8_t myIndex = dirIndex(mine);

  closeProcess(&allProcesses[schedulerPid - 1], otherProcess()->processId);

  NANO_OS_ASSERT_EQ_INT(1, stubDirsClosed);
  NANO_OS_ASSERT_EQ_INT(1, stubFilesClosed);
  NANO_OS_ASSERT_EQ_INT(0, handleState.openDirs[theirIndex].owner);
  NANO_OS_ASSERT_EQ_INT(self()->processId, handleState.openDirs[myIndex].owner);
  NANO_OS_ASSERT_EQ_INT(1, handleState.numOpenFiles);
  NANO_OS_ASSERT_EQ_PTR(myFile, handleState.openFiles);
  NANO_OS_ASSERT_NULL(handleState.openFiles->next);
  NANO_OS_ASSERT_NULL(handleState.openFiles->prev);
  (void) theirFile;
}

NANO_OS_KERNEL_TEST(fs_handles,
  the_old_handle_fails_for_a_process_given_the_pid
) {
  handleFixture();
  DIR *dirp = openDir(otherProcess());
  closeProcess(&allProcesses[schedulerPid - 1], otherProcess()->processId);

  // The same pid, so the same descriptor, asks with the dead process's handle.
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, readDir(otherProcess(), dirp));
}

NANO_OS_KERNEL_TEST(fs_handles, only_a_kernel_process_can_close_a_process) {
  handleFixture();
  NanoOsFile *file = openFile(otherProcess(), "r");
  DIR *dirp = openDir(otherProcess());

  PrivilegeLevel privilegeLevel = self()->privilegeLevel;
  self()->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  closeProcess(self(), otherProcess()->processId);
  self()->privilegeLevel = privilegeLevel;

  NANO_OS_ASSERT_EQ_INT(0, stubDirsClosed);
  NANO_OS_ASSERT_EQ_INT(0, stubFilesClosed);
  NANO_OS_ASSERT_EQ_PTR(file, handleState.openFiles);
  NANO_OS_ASSERT_EQ_INT(0, readDir(otherProcess(), dirp));
}

NANO_OS_KERNEL_TEST(fs_handles, closing_pid_zero_touches_no_free_slot) {
  handleFixture();
  DIR *dirp = openDir(self());
  NANO_OS_ASSERT_EQ_INT(0, closeDir(self(), dirp));
  FilesystemOpenDir *slot = &handleState.openDirs[dirIndex(dirp)];
  uint16_t generation = slot->generation;

  closeProcess(&allProcesses[schedulerPid - 1], 0);

  NANO_OS_ASSERT_EQ_INT(generation, slot->generation);
  NANO_OS_ASSERT_EQ_INT(1, stubDirsClosed);
}

/// @var fakeFile
///
/// @brief The file the fake filesystem process opens.
static NanoOsFile fakeFile;

/// @var fakeMode
///
/// @brief The mode of the last fopen the fake filesystem process received.
static char fakeMode[4];

/// @var fakeClosedPid
///
/// @brief The pid of the last FILESYSTEM_CLOSE_PROCESS received, or 0.
static volatile ProcessId fakeClosedPid;

/// @var fakeClosedCount
///
/// @brief The number of FILESYSTEM_CLOSE_PROCESS messages received.
static volatile int fakeClosedCount;

/// @var fakeIoError
///
/// @brief The errno value the fake filesystem process reports for I/O.
static volatile int fakeIoError;

/// @fn static void* fakeFilesystem(void *args)
///
/// @brief Stand in for the filesystem process.
///
/// @param args Not used.
///
/// @return This function never returns.
static void* fakeFilesystem(void *args) {
  (void) args;
  while (1) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (message == NULL) {
      continue;
    }

    void *data = processMessageData(message);
    switch (processMessageType(message) & 0xff) {
      case FILESYSTEM_CLOSE_PROCESS:
        fakeClosedPid = ((FilesystemCloseProcessArgs*) data)->pid;
        fakeClosedCount++;
        break;

      case FILESYSTEM_OPEN_FILE: {
        FilesystemFopenArgs *fopenArgs = (FilesystemFopenArgs*) data;
        strncpy(fakeMode, fopenArgs->mode, sizeof(fakeMode) - 1);
        fakeFile.fd = fopenArgs->fd;
        fopenArgs->returnValue = &fakeFile;
        break;
      }

      case FILESYSTEM_OPEN_DIR:
        ((FilesystemOpendirArgs*) data)->returnValue
          = (DIR*) (uintptr_t) 0x10;
        break;

      case FILESYSTEM_READ_FILE:
      case FILESYSTEM_WRITE_FILE:
        if (fakeIoError != 0) {
          ((FilesystemIoCommandArgs*) data)->length = 0;
          ((FilesystemIoCommandArgs*) data)->errorNumber = fakeIoError;
        }
        break;

      case FILESYSTEM_END_OF_FILE:
        ((FeofArgs*) data)->errorNumber = fakeIoError;
        break;

      default:
        break;
    }
    processMessageSetDone(message);
  }
  return NULL;
}

/// @fn static void useFakeFilesystem(void)
///
/// @brief Start the fake filesystem process and route filesystem messages to
/// it.
///
/// @return This function returns no value.
static void useFakeFilesystem(void) {
  fakeClosedPid = 0;
  fakeClosedCount = 0;
  fakeIoError = 0;
  memset(fakeMode, 0, sizeof(fakeMode));
  ProcessDescriptor *processDescriptor = processQueuePop(halMockFreeQueue());
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    processCreate(processDescriptor, fakeFilesystem, NULL));
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->userId = ROOT_USER_ID;
  // The scheduler's blocking send only runs the levels above supervisor.
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->readyQueue = otherProcess()->readyQueue;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  rootFilesystemPid = processDescriptor->processId;

  // The test body's array was built before this destination existed.
  ProcessDescriptor *running = self();
  static IpcCapability ipcCapabilities[64];
  NANO_OS_ASSERT_TRUE(running->numIpcCapabilities + 1 < 64);
  memcpy(ipcCapabilities, running->ipcCapabilities,
    sizeof(IpcCapability) * running->numIpcCapabilities);
  ipcCapabilityInsert(ipcCapabilities, running->numIpcCapabilities,
    rootFilesystemPid, FILESYSTEM_COMMAND_SIGNATURE, 0);
  running->ipcCapabilities = ipcCapabilities;
  running->numIpcCapabilities++;
  for (size_t ii = 0; ii < running->numIpcCapabilities; ii++) {
    if (ipcCapabilities[ii].destinationPid == rootFilesystemPid) {
      ipcCapabilities[ii].messageTypes = UINT32_MAX;
    }
  }

  for (int ii = 0; ii < 10; ii++) {
    processYield();
  }
}

/// @var childMayExit
///
/// @brief Holds a leakingChild until the filesystem has been redirected, so
/// that the spawn itself doesn't reach the fake filesystem.
static volatile bool childMayExit;

/// @fn static void* leakingChild(void *args)
///
/// @brief Stand in for a program, chosen by argv[0], that exits with handles
/// open ("leak") or with none.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function returns NULL.
static void* leakingChild(void *args) {
  while (!childMayExit) {
    processYield();
  }
  if (strcmp(((ExecArgs*) args)->argv[0], "leak") == 0) {
    getRunningProcess()->numFilesystemHandles = 2;
  }
  return NULL;
}

/// @fn static void useLeakingChild(void)
///
/// @brief Make spawned processes run leakingChild instead of an overlay.
///
/// @return This function returns no value.
static void useLeakingChild(void) {
  halMockSetExecCommand(leakingChild);
}

/// @fn static pid_t runChild(char *name)
///
/// @brief Spawn a leakingChild and wait for it to exit.
///
/// @param name The mode, passed as argv[0].
///
/// @return Returns the child's process ID.
static pid_t runChild(char *name) {
  pid_t pid = 0;
  char *argv[] = { name, NULL };
  childMayExit = false;
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSpawn(&pid, "/child", NULL, NULL, argv, NULL));
  useFakeFilesystem();
  childMayExit = true;
  NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, NULL, 0));
  return pid;
}

NANO_OS_KERNEL_TEST_WITH(fs_handles, a_process_exiting_with_handles_is_closed,
  useLeakingChild
) {
  pid_t pid = runChild("leak");
  NANO_OS_ASSERT_EQ_INT(1, fakeClosedCount);
  NANO_OS_ASSERT_EQ_INT(pid, fakeClosedPid);
  NANO_OS_ASSERT_EQ_INT(0, allProcesses[pid - 1].numFilesystemHandles);
}

NANO_OS_KERNEL_TEST_WITH(fs_handles, a_process_exiting_clean_costs_no_message,
  useLeakingChild
) {
  (void) runChild("clean");
  NANO_OS_ASSERT_EQ_INT(0, fakeClosedCount);
}

NANO_OS_KERNEL_TEST(fs_handles, handle_counts_follow_open_and_close) {
  useFakeFilesystem();
  ProcessDescriptor *running = self();
  uint8_t handles = running->numFilesystemHandles;

  DIR *dirp = filesystemOpendir("/tmp");
  NANO_OS_ASSERT_NOT_NULL(dirp);
  NANO_OS_ASSERT_EQ_INT(handles + 1, running->numFilesystemHandles);
  NANO_OS_ASSERT_EQ_INT(0, filesystemClosedir(dirp));
  NANO_OS_ASSERT_EQ_INT(handles, running->numFilesystemHandles);

  FILE *file = filesystemFopen("/tmp/f", "r");
  NANO_OS_ASSERT_NOT_NULL(file);
  NANO_OS_ASSERT_EQ_INT(handles + 1, running->numFilesystemHandles);
  NANO_OS_ASSERT_EQ_INT(0, filesystemFclose(file));
  NANO_OS_ASSERT_EQ_INT(handles, running->numFilesystemHandles);
}

NANO_OS_KERNEL_TEST(fs_handles, a_refused_io_sets_errno) {
  useFakeFilesystem();
  char buffer[4];
  FILE *file = filesystemFopen("/tmp/f", "r");
  NANO_OS_ASSERT_NOT_NULL(file);
  fakeIoError = FILESYSTEM_EBADF;

  errno = 0;
  NANO_OS_ASSERT_EQ_INT(0, (int) filesystemFRead(buffer, 1, 4, file));
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, errno);
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(0, (int) filesystemFWrite(buffer, 1, 4, file));
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, errno);
  errno = 0;
  (void) filesystemEndOfFile(file);
  NANO_OS_ASSERT_EQ_INT(FILESYSTEM_EBADF, errno);
}

NANO_OS_KERNEL_TEST(fs_handles, sethostname_opens_the_file_for_writing) {
  useFakeFilesystem();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSethostname("name", 4));
  NANO_OS_ASSERT_STR_EQ("w", fakeMode);
}
