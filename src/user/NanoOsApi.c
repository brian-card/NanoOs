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

// Standard C includes
#include <stdarg.h>
#include <stdint.h>

// Unix includes
#include "sys/types.h"

// NanoOs includes
#include "../kernel/NanoOsTypes.h"

// Types and prototypes we need here because we can't include things directly.
typedef uintptr_t size_t;
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

// Must come first
#include "NanoOsApi.h"

#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOs.h"
#include "../kernel/OverlayFunctions.h"
#include "../kernel/Scheduler.h"
#include "../kernel/Tasks.h"
#include "NanoOsFcntl.h"
#include "NanoOsLibC.h"
#include "NanoOsPwd.h"
#include "NanoOsSched.h"
#include "NanoOsSignal.h"
#include "NanoOsTermios.h"
#include "NanoOsUnistd.h"

// Must come last
#include "NanoOsStdio.h"

#undef stdin
#undef stdout
#undef stderr
#undef fopen
#undef fclose
#undef remove
#undef fseek
#undef vfscanf
#undef vfprintf
#undef fputs
#undef fgets
#undef fread
#undef fwrite
#undef strerror
#undef fileno

NanoOsApi nanoOsApi = {
  // Standard Unix functionality
  
  // Standard streams:
  .stdin  = (FILE*) ((intptr_t) 0x1),
  .stdout = (FILE*) ((intptr_t) 0x2),
  .stderr = (FILE*) ((intptr_t) 0x3),
  
  // File operations:
  .fopen = filesystemFOpen,
  .fclose = filesystemFClose,
  .remove = filesystemRemove,
  .fseek = filesystemFSeek,
  .fileno = nanoOsFileno,
  
  // Formatted I/O:
  .vsscanf = vsscanf,
  .vfscanf = nanoOsVfscanf,
  .vfprintf = nanoOsVfprintf,
  .vsnprintf = vsnprintf,
  
  // Direct I/O:
  .fread = nanoOsFread,
  .fwrite = nanoOsFwrite,
  
  // Memory management:
  .free = memoryManagerFree,
  .realloc = memoryManagerRealloc,
  .malloc = memoryManagerMalloc,
  .calloc = memoryManagerCalloc,
  
  // Copying functions:
  .memcpy = memcpy,
  .memmove = memmove,
  .strcpy = strcpy,
  .strncpy = strncpy,
  .strcat = strcat,
  .strncat = strncat,
  
  // Search functions:
  .memcmp = memcmp,
  .strcmp = strcmp,
  .strncmp = strncmp,
  .strstr = strstr,
  .strchr = strchr,
  .strrchr = strrchr,
  .strspn = strspn,
  .strcspn = strcspn,
  
  // Miscellaaneous string functions:
  .memset = memset,
  .strerror = nanoOsStrError,
  .strlen = strlen,
  
  // Other stdlib functions:
  .strtoll = nanoOsStrtoll,
  
  // unistd functions:
  .close = nanoOsClose,
  .dup2 = nanoOsDup2,
  .gethostname = nanoOsGethostname,
  .sethostname = nanoOsSethostname,
  .ttyname_r = nanoOsTtyname_r,
  .execve = schedulerExecve,
  .setuid = schedulerSetTaskUser,
  .pipe = nanoOsPipe,
  
  // termios functions:
  .tcgetattr = nanoOsTcgetattr,
  .tcsetattr = nanoOsTcsetattr,
  
  // errno functions:
  .errno_ = errno_,
  
  // sys/*.h functions:
  .uname = nanoOsUname,
  
  // time.h functions:
  .time = time,
  
  // pwd.h functions:
  .getpwnam_r = nanoOsGetpwnam_r,
  .getpwuid_r = nanoOsGetpwuid_r,
  
  // sched.h functions:
  .sched_yield = nanoOsSchedYield,
  
  // signal.h functions:
  .kill = nanoOsKill,
  
  // spawn.h functions:
  .posix_spawn_file_actions_init = nanoOsSpawnFileActionsInit,
  .posix_spawn_file_actions_adddup2 = nanoOsSpawnFileActionsAdddup2,
  .posix_spawn_file_actions_destroy = nanoOsSpawnFileActionsDestroy,
  .posix_spawn = nanoOsSpawn,
  
  // fcntl.h functions:
  .fcntl = nanoOsFcntl,
  
  // NanoOs-specific functionality
  
  // NanoOsUser.h functions:
  .callOverlayFunction = callOverlayFunction,
  
  // NanoOsUtils.h functions:
  .parseArgs = parseArgs,
  .getFreeMemory = getFreeMemory,
  
  // NanoOsTasks.h functions:
  .getTaskInfo = schedulerGetTaskInfo,
  
  // NanoOsHardware.h functions:
  .shutdown = nanoOsHardwareShutdown,
  
  // Debug functions
  .printString = printString_,
  .printInt = printInt_,
  .printDouble = printDouble,
  .printHex = printHex_,
};

