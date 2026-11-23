////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

// Standard C includes
#include <stdarg.h>
#include <stdint.h>

// Unix includes
#include "sys/types.h"

// NanoOs includes
#include "../kernel/NanoOsTypes.h"

// Types and prototypes we need here because we can't include things directly.
typedef uintptr_t size_t;
int nanoOsVsnprintf(char *buffer, size_t size, const char *format, va_list args);
char** parseArgs(char *command, int *argc);

// Must come first
#include "NanoOsApi.h"

#include "../kernel/Console.h"
#include "../kernel/Coroutines.h"
#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOs.h"
#include "../kernel/OverlayFunctions.h"
#include "../kernel/Scheduler.h"
#include "../kernel/Processes.h"
#include "../hal/HalCommon.h"
#include "NanoOsDirCap.h"
#include "NanoOsFcntl.h"
#include "NanoOsLibC.h"
#include "NanoOsPwd.h"
#include "NanoOsSched.h"
#include "NanoOsSignal.h"
#include "NanoOsTermios.h"
#include "NanoOsTime.h"
#undef timezone
#undef time
#undef gmtime_r
#undef localtime_r
#undef timespec_get
#include "NanoOsUnistd.h"

// Must come last
#include "NanoOsStdio.h"

#undef stdin
#undef stdout
#undef stderr
#undef fopen
#undef fclose
#undef remove
#undef rename
#undef mkdir
#undef rmdir
#undef fseek
#undef vfscanf
#undef vfprintf
#undef vsnprintf
#undef fputs
#undef fgets
#undef fread
#undef fwrite
#undef feof
#undef ftell
#undef strerror
#undef fileno
#undef opendir
#undef readdir
#undef closedir
#undef lstat
#undef dirstat

/// @fn static void* overlayMessageArgsCreate(size_t size, uint8_t ownership)
///
/// @brief Allocate zeroed message argument storage for an overlay.  This
/// exists because MESSAGE_ARGS_CREATE_BYTES is a macro whose expansion depends
/// on the kernel's configuration, which an overlay can't see.
///
/// @param size The number of bytes to allocate.
/// @param ownership The MESSAGE_ARGS_* ownership of the storage.
///
/// @return Returns a pointer to the storage on success, NULL on failure.
static void* overlayMessageArgsCreate(size_t size, uint8_t ownership) {
  (void) ownership;
  return MESSAGE_ARGS_CREATE_BYTES(size, ownership);
}

/// @fn static void overlayMessageArgsDestroy(void *ptr)
///
/// @brief Free message argument storage an overlay allocated.  This exists
/// because MESSAGE_ARGS_DESTROY is a macro whose expansion depends on the
/// kernel's configuration, which an overlay can't see.
///
/// @param ptr The storage, or NULL.
///
/// @return This function returns no value.
static void overlayMessageArgsDestroy(void *ptr) {
  MESSAGE_ARGS_DESTROY(ptr);
}

/// @fn static void* overlayMessageArgsAllocate(size_t size)
///
/// @brief Allocate zeroed storage for an overlay's scoped, blocking,
/// round-trip message.  Overlays call this only when messageArgsChecked is
/// set, because in production their MESSAGE_ARGS_ALLOCATE uses the stack.
///
/// @param size The number of bytes to allocate.
///
/// @return Returns a pointer to the storage on success, NULL on failure or in
/// production.
static void* overlayMessageArgsAllocate(size_t size) {
#ifdef NANO_OS_MESSAGE_ARGS_CHECKED
  return messageArgsAllocate(size, MESSAGE_ARGS_ROUND_TRIP, false);
#else
  (void) size;
  return NULL;
#endif
}

/// @fn static void overlayMessageArgsFree(void *ptr)
///
/// @brief Free storage from overlayMessageArgsAllocate.
///
/// @param ptr The storage.
///
/// @return This function returns no value.
static void overlayMessageArgsFree(void *ptr) {
  MESSAGE_ARGS_FREE(ptr);
}

/// @fn static void* overlayMessageBufferShare(void *ptr, size_t size,
///   uint8_t direction, uint8_t recipient)
///
/// @brief Share an overlay's buffer with the process a message carrying it is
/// sent to.
///
/// @param ptr The buffer.
/// @param size The number of bytes in the buffer.
/// @param direction The MESSAGE_BUFFER_* direction of the buffer's data.
/// @param recipient The ProcessId of the process the buffer is shared with.
///
/// @return Returns the pointer to put in the argument structure, or NULL if
/// the buffer couldn't be shared.
static void* overlayMessageBufferShare(void *ptr, size_t size,
  uint8_t direction, uint8_t recipient
) {
  (void) size;
  (void) direction;
  (void) recipient;
  return MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient);
}

/// @fn static void overlayMessageBufferUnshare(void *shared, void *original,
///   uint8_t direction)
///
/// @brief End the sharing of an overlay's buffer once the exchange is done.
///
/// @param shared What overlayMessageBufferShare returned.
/// @param original The buffer that was shared.
/// @param direction The MESSAGE_BUFFER_* direction it was shared with.
///
/// @return This function returns no value.
static void overlayMessageBufferUnshare(void *shared, void *original,
  uint8_t direction
) {
  (void) shared;
  (void) original;
  (void) direction;
  MESSAGE_BUFFER_UNSHARE(shared, original, direction);
}

KEEP_IN_FLASH
const NanoOsExecutiveApi nanoOsExecutiveApi = {
  // Debug functions:
  .printString = printString_,
  .printInt = printInt_,
  .printDouble = printDouble,
  .printHex = printHex_,
  
  // Kernel process functions:
  .getRunningCoroutine = getRunningCoroutine,
  .coroutineContext = coroutineContext,
  .coroutineSetContext = coroutineSetContext,
  .coroutineStackEnd = coroutineStackEnd,
  .coroutineSetStackEnd = coroutineSetStackEnd,
  .coroutineStackOverflowed = coroutineStackOverflowed,
  .coroutineYield = coroutineYield,
  
  // Kernel IPC functions:
  .findProcessByName = findProcessByName,

  // Directory capability functions:
  .dirCapabilityAllows = dirCapabilityAllows,

  // Full-path functions:
  .execveFullPath = schedulerExecveFullPath,
  .fopenFullPath = filesystemFopenFullPath,
  .renameFullPath = filesystemRenameFullPath,
  .mkdirFullPath = filesystemMkdirFullPath,
  .rmdirFullPath = filesystemRmdirFullPath,
};

KEEP_IN_FLASH
static const NanoOsUserApi _nanoOsUserApi = {
  // Standard Unix functionality
  
  // Standard streams:
  .stdin  = (FILE*) ((intptr_t) 0x1),
  .stdout = (FILE*) ((intptr_t) 0x2),
  .stderr = (FILE*) ((intptr_t) 0x3),
  
  // File operations:
  .fopen = filesystemFopen,
  .fclose = filesystemFclose,
  .remove = filesystemRemove,
  .rename = filesystemRename,
  .mkdir = filesystemMkdir,
  .rmdir = filesystemRmdir,
  .fseek = filesystemFSeek,
  .fileno = nanoOsFileno,
  .feof = filesystemEndOfFile,
  .ftell = filesystemFtell,

  // Directory operations:
  .opendir = filesystemOpendir,
  .readdir = filesystemReaddir,
  .closedir = filesystemClosedir,

  // File status:
  .lstat = filesystemLstat,
  .dirstat = filesystemDirstat,

  // Formatted I/O:
  .vsscanf = vsscanf,
  .vfscanf = nanoOsVfscanf,
  .vfprintf = nanoOsVfprintf,
  .vsnprintf = nanoOsVsnprintf,
  
  // Direct I/O:
  .fread = nanoOsFread,
  .fgets = nanoOsFgets,
  .fwrite = nanoOsFwrite,
  
  // Memory management:
  .free = memoryManagerFree,
  .realloc = memoryManagerRealloc,
  .malloc = memoryManagerMalloc,
  .calloc = memoryManagerCalloc,
  
  // Copying functions:
  .memcpy = nanoOsCheckedMemcpy,
  .memmove = nanoOsCheckedMemmove,
  .strcpy = nanoOsCheckedStrcpy,
  .strncpy = nanoOsCheckedStrncpy,
  .strcat = nanoOsCheckedStrcat,
  .strncat = nanoOsCheckedStrncat,
  
  // Search functions:
  .memcmp = nanoOsCheckedMemcmp,
  .strcmp = nanoOsCheckedStrcmp,
  .strncmp = nanoOsCheckedStrncmp,
  .strstr = nanoOsCheckedStrstr,
  .strchr = nanoOsCheckedStrchr,
  .strrchr = nanoOsCheckedStrrchr,
  .strspn = nanoOsCheckedStrspn,
  .strcspn = nanoOsCheckedStrcspn,
  
  // Miscellaaneous string functions:
  .memset = nanoOsCheckedMemset,
  .strerror = nanoOsStrError,
  .strlen = nanoOsCheckedStrlen,
  
  // Other stdlib functions:
  .strtoll = nanoOsStrtoll,
  .setenv = nanoOsSetenv,
  .unsetenv = nanoOsUnsetenv,
  
  // unistd functions:
  .close = nanoOsClose,
  .dup = nanoOsDup,
  .dup2 = nanoOsDup2,
  .gethostname = nanoOsGethostname,
  .getpid = nanoOsGetpid,
  .getuid = nanoOsGetuid,
  .sethostname = nanoOsSethostname,
  .ttyname_r = nanoOsTtyname_r,
  .execve = schedulerExecve,
  .setuid = schedulerSetProcessUser,
  .pipe = nanoOsPipe,
  .sleep = nanoOsSleep,
  
  // termios functions:
  .tcgetattr = nanoOsTcgetattr,
  .tcsetattr = nanoOsTcsetattr,
  .tcgetwinsize = nanoOsTcgetwinsize,
  .tcsetwinsize = nanoOsTcsetwinsize,
  
  // errno functions:
  .errno_ = errno_,
  
  // sys/*.h functions:
  .uname = nanoOsUname,
  
  // time.h functions:
  .nanoOsTimezone = nanoOsTimezone,
  .nanoOsDstInEffect = nanoOsDstInEffect,
  .nanoOsTime = nanoOsTime,
  .nanoOsGmtime_r = nanoOsGmtime_r,
  .nanoOsLocaltime_r = nanoOsLocaltime_r,
  .nanoOsTimespec_get = nanoOsTimespec_get,
  .nanoOsNanosleep = nanoOsNanosleep,
  
  // pwd.h functions:
  .getpwnam_r = nanoOsGetpwnam_r,
  .getpwuid_r = nanoOsGetpwuid_r,
  
  // grp.h functions:
  .getgrnam_r = nanoOsGetgrnam_r,
  .getgrgid_r = nanoOsGetgrgid_r,
  
  // sched.h functions:
  .sched_yield = nanoOsSchedYield,
  
  // signal.h functions:
  .kill = nanoOsKill,
  .waitpid = schedulerWaitpid,
  
  // spawn.h functions:
  .posix_spawn_file_actions_init = nanoOsSpawnFileActionsInit,
  .posix_spawn_file_actions_adddup2 = nanoOsSpawnFileActionsAdddup2,
  .posix_spawn_file_actions_destroy = nanoOsSpawnFileActionsDestroy,
  .posix_spawn = nanoOsSpawn,
  .posix_spawnattr_adddircap = nanoOsSpawnattrAdddircap,
  
  // fcntl.h functions:
  .fcntl = nanoOsFcntl,
  
  // NanoOs-specific functionality
  
  // NanoOsUtils.h functions:
  .parseArgs = parseArgs,
  .pathCollapse = pathCollapse,
  .getFreeMemory = getFreeMemory,
  .dumpMemoryAllocations = dumpMemoryAllocations,
  
  // NanoOsProcesses.h functions:
  .getProcessInfo = schedulerGetProcessInfo,

  // sys/dircap.h functions:
  .dircapAdd = nanoOsDircapAdd,
  .dircapDrop = nanoOsDircapDrop,
  .dircapGet = nanoOsDircapGet,
  
  // NanoOsHardware.h functions:
  .shutdown = nanoOsHardwareShutdown,
  
  // NanoOsConsole.h functions:
  .getConsoleColors = getConsoleColors,
  .setConsoleColors = setConsoleColors,

  // Kernel message functions, which IPC capabilities guard:
  .processMessageInit = msg_init,
  .processMessageRelease = msg_release,
  .processMessageSetDone = msg_set_done,
  .processMessageWaitForDone = msg_wait_for_done,
  .processMessageElement = msg_element,
  
  // Kernel message queue functions:
  .comessageQueuePeek = comessageQueuePeek,
  .comessageQueuePop = comessageQueuePop,
  .comessageQueuePopType = comessageQueuePopType,
  .comessageQueueWait = comessageQueueWait,
  .comessageQueueWaitForType = comessageQueueWaitForType,

  // Kernel IPC functions:
  .getAvailableMessage = getAvailableMessage,
  .sendProcessMessageToPid = sendProcessMessageToPid,

  // Message argument storage functions:
  .messageArgsCreate = overlayMessageArgsCreate,
  .messageArgsDestroy = overlayMessageArgsDestroy,
#ifdef NANO_OS_MESSAGE_ARGS_CHECKED
  .messageArgsChecked = true,
#else
  .messageArgsChecked = false,
#endif
  .messageArgsAllocate = overlayMessageArgsAllocate,
  .messageArgsFree = overlayMessageArgsFree,
  .messageBufferShare = overlayMessageBufferShare,
  .messageBufferUnshare = overlayMessageBufferUnshare,
};

NanoOsApi nanoOsApi = {
  // Base API for user space processes:
  .userApi = &_nanoOsUserApi,
  
  // NanoOsUser.h functions:
  .callOverlayFunction = NULL,
  
  // Additional API for PRIVILEGE_LEVEL_EXECUTIVE processes only:
  .executiveApi = NULL,
  
  // HAL access for PRIVILEGE_LEVEL_EXECUTIVE processes only:
  .hal = NULL,
};

NanoOsApi *NANO_OS_API = NULL;

