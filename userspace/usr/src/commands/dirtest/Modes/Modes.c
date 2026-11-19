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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              Modes.c
///
/// @brief             The dirtest modes that an argument selects.
///
///////////////////////////////////////////////////////////////////////////////

#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dircap.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "NanoOsUser.h"

/// @brief Print the name of every entry in a directory, one per line.
///
/// @param path The directory.
///
/// @return This function returns no value.
static void listNames(const char *path) {
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    printf("opendir failed, errno=%d\n", errno);
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    // printf stops at the console buffer's size, and a name can be longer.
    fputs(entry->d_name, stdout);
    fputs("\n", stdout);
  }
  closedir(dirp);
}

/// @brief Read two directories in turn, printing both current entries after
/// each pair of reads so that one read overwriting the other's entry shows.
///
/// @param firstPath The first directory.
/// @param secondPath The second directory.
///
/// @return This function returns no value.
static void interleaveDirs(const char *firstPath, const char *secondPath) {
  DIR *first = opendir(firstPath);
  DIR *second = opendir(secondPath);
  if ((first != NULL) && (second != NULL)) {
    struct dirent *firstEntry = readdir(first);
    struct dirent *secondEntry = readdir(second);
    while ((firstEntry != NULL) || (secondEntry != NULL)) {
      printf("a=\"%s\" b=\"%s\"\n",
        (firstEntry != NULL) ? firstEntry->d_name : "",
        (secondEntry != NULL) ? secondEntry->d_name : "");
      if (firstEntry != NULL) {
        firstEntry = readdir(first);
      }
      if (secondEntry != NULL) {
        secondEntry = readdir(second);
      }
    }
  } else {
    printf("opendir failed, errno=%d\n", errno);
  }

  if (first != NULL) {
    closedir(first);
  }
  if (second != NULL) {
    closedir(second);
  }
}

/// @brief Create an empty file in a directory whose name is the longest a
/// name can be, 255 characters of repeating digits.
///
/// @param dirPath The directory.
///
/// @return This function returns no value.
static void createLongName(const char *dirPath) {
  size_t dirLength = strlen(dirPath);
  char *path = (char*) malloc(dirLength + 1 + 255 + 1);
  if (path == NULL) {
    printf("malloc failed\n");
    return;
  }

  strcpy(path, dirPath);
  path[dirLength] = '/';
  for (int ii = 0; ii < 255; ii++) {
    path[dirLength + 1 + ii] = (char) ('0' + (ii % 10));
  }
  path[dirLength + 1 + 255] = '\0';

  FILE *file = fopen(path, "w");
  if (file == NULL) {
    printf("create failed, errno=%d\n", errno);
  } else {
    fclose(file);
    printf("created\n");
  }
  free(path);
}

/// @brief Create a file holding one line.
///
/// @param path The path of the file.
///
/// @return This function returns no value.
static void writeLine(const char *path) {
  FILE *file = fopen(path, "w");
  if (file != NULL) {
    // Copied off the overlay, which another process's can replace before the
    // filesystem reads it.
    char line[] = "original\n";
    fputs(line, file);
    fclose(file);
  }
}

/// @brief Print the first line of a file.
///
/// @param path The path of the file.
///
/// @return This function returns no value.
static void showLine(const char *path) {
  char line[16] = {0};
  FILE *file = fopen(path, "r");
  if (file != NULL) {
    fgets(line, sizeof(line), file);
    fclose(file);
  }
  printf("%s holds %s", path, line);
}

/// @brief Print the outcome of a call that returned an int.
///
/// @param label What the call was.
/// @param result What it returned.
///
/// @return This function returns no value.
static void showResult(const char *label, int result) {
  printf("%s -> %d, errno=%d\n", label, result, errno);
  errno = 0;
}

/// @brief Open a file, print the outcome, and close it if it opened.
///
/// @param label What the call was.
/// @param path The file's path.
/// @param mode The fopen mode.
///
/// @return This function returns no value.
static void showOpen(const char *label, const char *path, const char *mode) {
  FILE *file = fopen(path, mode);
  showResult(label, (file == NULL) ? -1 : 0);
  if (file != NULL) {
    fclose(file);
  }
}

/// @brief Narrow the process's set so that the filesystem driver has to
/// refuse to create and to replace a file, and show that it does.
///
/// @return This function returns no value.
static void showAccess(void) {
  struct stat st;
  mkdir("/tmp/acc", 0777);
  mkdir("/tmp/acc/ro", 0777);
  mkdir("/tmp/acc/from", 0777);
  mkdir("/tmp/acc/to", 0777);
  writeLine("/tmp/acc/ro/old");
  writeLine("/tmp/acc/from/x");
  writeLine("/tmp/acc/to/y");

  uint8_t noCreate = DIRCAP_READ | DIRCAP_WRITE | DIRCAP_LIST | DIRCAP_STAT;
  dircapAdd("/tmp/acc/ro/", noCreate);
  dircapAdd("/tmp/acc/from/", DIRCAP_READ | DIRCAP_DELETE | DIRCAP_STAT);
  dircapAdd("/tmp/acc/to/", DIRCAP_READ | DIRCAP_CREATE | DIRCAP_STAT);
  dircapDrop("/tmp/");
  errno = 0;

  FILE *created = fopen("/tmp/acc/ro/new", "w");
  showResult("create without c", (created == NULL) ? -1 : 0);
  showResult("stat refused create", lstat("/tmp/acc/ro/new", &st));

  FILE *existing = fopen("/tmp/acc/ro/old", "a");
  showResult("append to existing", (existing == NULL) ? -1 : 0);
  fclose(existing);

  showResult("remove without d", remove("/tmp/acc/ro/old"));
  showResult("mkdir without c", mkdir("/tmp/acc/ro/dir", 0777));
  showResult("rmdir without d", rmdir("/tmp/acc/ro"));

  showResult("replace without d", rename("/tmp/acc/from/x", "/tmp/acc/to/y"));
  showResult("stat source", lstat("/tmp/acc/from/x", &st));
  showLine("/tmp/acc/to/y");
  showResult("rename to new name", rename("/tmp/acc/from/x", "/tmp/acc/to/z"));
  showResult("stat renamed", lstat("/tmp/acc/to/z", &st));

  showOpen("create without w", "/tmp/acc/to/new", "w");
  showResult("stat refused create without w", lstat("/tmp/acc/to/new", &st));
  showOpen("write existing without w", "/tmp/acc/to/y", "w");
  showLine("/tmp/acc/to/y");
}

/// @brief Print the process's directory capabilities, one per line.
///
/// @return This function returns no value.
static void showCapabilities(void) {
  int size = dircapGet(NULL, 0);
  DirCapabilities *set = (size > 0) ? (DirCapabilities*) malloc(size) : NULL;
  if ((set == NULL) || (dircapGet(set, size) != size)) {
    printf("dircapGet failed\n");
    free(set);
    return;
  }

  DirCapability *capability = set->capabilities;
  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    printf("cap %02x %.*s\n", (unsigned int) capability->mask,
      (int) capability->len, capability->name);
    capability = (DirCapability*) &capability->name[capability->len];
  }
  free(set);
}

/// @brief Run a program with POSIX_SPAWN_SYSTEM in the foreground and print
/// what the spawn returned and how the program exited.
///
/// @param argv The name to run followed by its arguments.
///
/// @return This function returns no value.
static void runSystemCommand(char **argv) {
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SYSTEM | POSIX_SPAWN_FOREGROUND);
  pid_t pid = 0;
  int returnValue = posix_spawn(&pid, argv[0], NULL, &attr, argv, environ);
  posix_spawnattr_destroy(&attr);
  int status = 0;
  if (returnValue == 0) {
    waitpid(pid, &status, 0);
  }
  printf("system spawn %d status %d\n", returnValue, WEXITSTATUS(status));
}

/// @fn void* dirtestMode(void *args)
///
/// @brief Run the dirtest mode its arguments name: "access", "caps",
/// "system <name> [argument...]", "list <dir>", "interleave <dir> <dir>", or
/// "long <dir>".
///
/// @param args The NULL-terminated arguments, starting with the mode.
///
/// @return Returns args if the arguments name a mode, NULL if not.
void* dirtestMode(void *args) {
  char **argv = (char**) args;
  if (strcmp(argv[0], "access") == 0) {
    showAccess();
  } else if (strcmp(argv[0], "caps") == 0) {
    showCapabilities();
  } else if (argv[1] == NULL) {
    return NULL;
  } else if (strcmp(argv[0], "system") == 0) {
    runSystemCommand(&argv[1]);
  } else if (strcmp(argv[0], "list") == 0) {
    listNames(argv[1]);
  } else if ((strcmp(argv[0], "interleave") == 0) && (argv[2] != NULL)) {
    interleaveDirs(argv[1], argv[2]);
  } else if (strcmp(argv[0], "long") == 0) {
    createLongName(argv[1]);
  } else {
    return NULL;
  }
  return args;
}
