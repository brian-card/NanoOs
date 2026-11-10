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
/// @file main.c
///
/// @brief Test-fixture command for the e2e regression suite (see
/// test/e2e/tests/dirtest.py): exercises opendir/readdir/closedir
/// against the real FAT32 image every e2e test runs against.  Not a
/// general-purpose utility -- follows the same pattern as the looseLoop and
/// tightLoop commands, which likewise exist only to give e2e tests something
/// to invoke.

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dircap.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "NanoOsUtils.h"

static void listDir(const char *path) {
  printf("opendir(%s):\n", path);
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    printf("  opendir failed, errno=%d\n", errno);
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    printf("  d_name=\"%s\" d_type=%d d_ino=%llu d_reclen=%u\n",
      entry->d_name, (int) entry->d_type,
      (unsigned long long) entry->d_ino, (unsigned int) entry->d_reclen);
  }

  closedir(dirp);
}

/// @brief Show what dirstat reports for /etc's first entry, and how it fails.
/// Must run last, because it takes STAT away from the process on /etc/.
///
/// @return This function returns no value.
static void showDirstat(void) {
  struct stat st;

  DIR *dirp = opendir("/etc");
  struct dirent *entry = readdir(dirp);
  ino_t ino = entry->d_ino;
  int result = dirstat(entry, &st);
  printf("dirstat -> %d, ino_matches=%d\n", result, st.st_ino == ino);

  entry->d_ino = ino + 1;
  result = dirstat(entry, &st);
  printf("dirstat changed d_ino -> %d, ino_matches=%d\n",
    result, st.st_ino == ino);

  closedir(dirp);
  errno = 0;
  result = dirstat(entry, &st);
  printf("dirstat after closedir -> %d, errno=%d\n", result, errno);

  dircapAdd("/etc/", DIRCAP_LIST);
  dirp = opendir("/etc");
  entry = readdir(dirp);
  errno = 0;
  result = dirstat(entry, &st);
  printf("dirstat without STAT -> %d, errno=%d\n", result, errno);
  closedir(dirp);
}

/// @brief Create a file holding one line.
///
/// @param path The path of the file.
///
/// @return This function returns no value.
static void writeLine(const char *path) {
  FILE *file = fopen(path, "w");
  if (file != NULL) {
    fputs("original\n", file);
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

/// @brief Print the usage summary for dirtest.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: dirtest [access|caps|system <name> [argument...]]\n"
    "\n"
    "Exercise the directory-reading functions.  List \"/\", \"/etc\", and \"/usr\"\n"
    "with opendir and readdir, showing each entry's name, type, inode, and\n"
    "record length, then show how opendir, readdir, and closedir handle a\n"
    "missing path, a regular file, the end of a directory, and a NULL handle,\n"
    "and how dirstat handles a changed d_ino, a closed directory, and a\n"
    "directory opened without the stat capability.  With \"access\", narrow\n"
    "the process's capabilities and show the operations they refuse.  With\n"
    "\"caps\", print the process's capabilities.  With \"system\", run a\n"
    "program from /system/commands with the arguments given.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }

  if ((argc > 1) && (strcmp(argv[1], "access") == 0)) {
    showAccess();
    return 0;
  }

  if ((argc > 1) && (strcmp(argv[1], "caps") == 0)) {
    showCapabilities();
    return 0;
  }

  if ((argc > 2) && (strcmp(argv[1], "system") == 0)) {
    runSystemCommand(&argv[2]);
    return 0;
  }

  // Fixed scenario the e2e tests assert against: test/e2e/mkimage.sh always
  // builds a root directory containing exactly /etc and /usr (each in turn
  // containing known files/subdirectories), so this needs no arguments.
  listDir("/");
  listDir("/etc");
  listDir("/usr");

  errno = 0;
  DIR *missing = opendir("/etc/nonexistent");
  printf("opendir(/etc/nonexistent) -> %s, errno=%d\n",
    (missing == NULL) ? "NULL" : "non-NULL", errno);

  errno = 0;
  DIR *notADir = opendir("/etc/hostname");
  printf("opendir(/etc/hostname) -> %s, errno=%d, strerror=\"%s\"\n",
    (notADir == NULL) ? "NULL" : "non-NULL", errno, strerror(errno));

  // readdir must not disturb errno when it returns NULL because the
  // directory was simply exhausted (not a real error).
  DIR *etc = opendir("/etc");
  while (readdir(etc) != NULL) {
    // Drain the directory.
  }
  errno = 0;
  struct dirent *pastEnd = readdir(etc);
  printf("readdir past end of /etc -> %s, errno=%d\n",
    (pastEnd == NULL) ? "NULL" : "non-NULL", errno);
  closedir(etc);

  // closedir on a NULL DIR must fail and set errno.
  errno = 0;
  int closeResult = closedir(NULL);
  printf("closedir(NULL) -> %d, errno=%d\n", closeResult, errno);

  showDirstat();

  return 0;
}
