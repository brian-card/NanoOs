///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              09.01.2025
///
/// @file              NanoOsApi.h
///
/// @brief             Functionality from Single Specification API plus any
///                    OS-specific functionality that is to be exported to user
///                    programs.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_USER_API_H
#define NANO_OS_USER_API_H

#undef FILE

#define FILE C_FILE
#include "stdarg.h"
#include "stddef.h"
#include "stdint.h"
#include "time.h"
#undef FILE
#undef stdin
#undef stdout
#undef stderr

#include "NanoOsHardware.h"
#include "NanoOsPwd.h"
#include "NanoOsSpawn.h"
#include "NanoOsSysUtsname.h"
#include "NanoOsSysTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct NanoOsFile NanoOsFile;
#define FILE NanoOsFile

typedef struct TaskInfo TaskInfo;

// POSIX-mandated objects require for posix_spawn
typedef struct posix_spawn_file_actions_t posix_spawn_file_actions_t;
typedef struct posix_spawnattr_t posix_spawnattr_t;

// Forward declarations from other headers.
struct termios;

typedef struct NanoOsApi {
  // Standard streams:
  FILE *stdin;
  FILE *stdout;
  FILE *stderr;
  
  // File operations:
  FILE* (*fopen)(const char *pathname, const char *mode);
  int (*fclose)(FILE *stream);
  int (*remove)(const char *pathname);
  int (*fseek)(FILE *stream, long offset, int whence);
  int (*fileno)(FILE *stream);
  
  // Formatted I/O:
  int (*vsscanf)(const char *buffer, const char *format, va_list args);
  int (*vfscanf)(FILE *stream, const char *format, va_list ap);
  int (*vfprintf)(FILE *stream, const char *format, va_list args);
  int (*vsnprintf)(char *str, size_t size, const char *format, va_list ap);
  
  // Direct I/O:
  size_t (*fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
  size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream);
  
  // Memory management:
  void (*free)(void *ptr);
  void* (*realloc)(void *ptr, size_t size);
  void* (*malloc)(size_t size);
  void* (*calloc)(size_t nmemb, size_t size);
  
  // Copying functions:
  void* (*memcpy)(void *dest, const void *src, size_t n);
  void* (*memmove)(void *dest, const void *src, size_t n);
  char* (*strcpy)(char *dst, const char *src);
  char* (*strncpy)(char *dst, const char *src, size_t dsize);
  char* (*strcat)(char *dst, const char *src);
  char* (*strncat)(char *dst, const char *src, size_t ssize);
  
  // Search functions:
  int (*memcmp)(const void *s1, const void *s2, size_t n);
  int (*strcmp)(const char *s1, const char *s2);
  int (*strncmp)(const char *s1, const char *s2, size_t n);
  char* (*strstr)(const char *haystack, const char *needle);
  char* (*strchr)(const char *s, int c);
  char* (*strrchr)(const char *s, int c);
  size_t (*strspn)(const char *s, const char *accept);
  size_t (*strcspn)(const char *s, const char *reject);
  
  // Miscellaaneous string functions:
  void* (*memset)(void *s, int c, size_t n);
  char* (*strerror)(int errnum);
  size_t (*strlen)(const char *s);
  
  // Other stdlib functions:
  long long (*strtoll)(const char *nptr, char **endptr, int base);
  
  // unistd functions:
  int (*close)(int fd);
  int (*dup2)(int oldfd, int newfd);
  int (*gethostname)(char *name, size_t len);
  int (*sethostname)(const char *name, size_t len);
  int (*ttyname_r)(int fd, char *buf, size_t buflen);
  int (*execve)(const char *pathname, char *const argv[], char *const envp[]);
  int (*setuid)(uid_t uid);
  int (*pipe)(int pipefd[2]);
  
  // termios functions:
  int (*tcgetattr)(int fd, struct termios *termios_p);
  int (*tcsetattr)(int fd, int optional_actions,
    const struct termios *termios_p);
  
  // errno functions:
  int* (*errno_)(void);
  
  // sys/*.h functions:
  int (*uname)(struct utsname *buf);
  
  // time.h functions:
  time_t (*time)(time_t *tloc);
  
  // pwd.h functions:
  int (*getpwnam_r)(
    const char *name,
    struct passwd *pwd,
    char *buf,
    size_t buflen,
    struct passwd **result);
  int (*getpwuid_r)(
    uid_t uid,
    struct passwd *pwd,
    char *buf,
    size_t buflen,
    struct passwd **result);
  
  // sched.h functions:
  int (*sched_yield)(void);
  
  // signal.h functions:
  int (*kill)(pid_t pid, int sig);
  
  // spawn.h functions:
  int (*posix_spawn_file_actions_init)(
    posix_spawn_file_actions_t *file_actions);
  int (*posix_spawn_file_actions_adddup2)(
    posix_spawn_file_actions_t *file_actions,
    int fildes,
    int newfildes);
  int (*posix_spawn_file_actions_destroy)(
    posix_spawn_file_actions_t *file_actions);
  int (*posix_spawn)(pid_t *pid, const char *path,
    const posix_spawn_file_actions_t *file_actions,
    const posix_spawnattr_t *attrp,
    char *const argv[], char *const envp[]);
  
  // fcntl.h functions:
  int (*fcntl)(int fd, int op, va_list arg);
  
  // NanoOs-specific functionality
  
  // NanoOsUser.h functions:
  void* (*callOverlayFunction)(const char *overlayPath, const char *overlay,
    const char *function, void *args);
  
  // NanoOsUtils.h functions:
  char** (*parseArgs)(char *command, int *argc);
  size_t (*getFreeMemory)(void);
  
  // NanoOsTasks.h functions:
  TaskInfo* (*getTaskInfo)(void);
  
  // NanoOsHardware.h functions:
  int (*shutdown)(NanoOsShutdownType shutdownType);
  
  // Debug functions
  int (*printString)(const char *string);
  int (*printInt)(long long int integer);
  int (*printDouble)(double floatingPointValue);
  int (*printHex)(unsigned long long int integer);
} NanoOsApi;

extern NanoOsApi nanoOsApi;

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_API_H

