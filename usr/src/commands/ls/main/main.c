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
/// @brief Standard Unix `ls` command implementation.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

/// @def INVALID_UID
///
/// @brief Sentinal value to represent an invalid user ID.
#define INVALID_UID ((uid_t) -1)

/// @def INVALID_GID
///
/// @brief Sentinal value to represent an invalid group ID.
#define INVALID_GID ((gid_t) -1)

/// @var weekdays
///
/// @brief Three-letter abbreviations for the days of the week.
static const char *weekdays[7] = {
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat",
};

/// @fn void listDir(const char *path)
///
/// @brief List the contents of a directory.
///
/// @param path The full path to the directory.
///
/// @return This function returns no value.
void listDir(const char *path) {
  uid_t lastUid = INVALID_UID;
  char lastUsername[NANO_OS_MAX_USERNAME_LENGTH];
  gid_t lastGid = INVALID_GID;
  char lastGroupname[NANO_OS_MAX_GROUPNAME_LENGTH];

  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    puts(strerror(errno));
    return;
  }

  struct dirent *entry;
  char buffer[96];
  struct stat st;
  struct tm tm;
  while ((entry = readdir(dirp)) != NULL) {
    if (istat(entry->d_ino, &st) < 0) {
      printf("%s%s\n", entry->d_name, (entry->d_type == DT_DIR) ? "/" : "");
      continue;
    }

    // st is valid.  Make sure our username and groupname are filled in.
    if (st.st_uid != lastUid) {
      struct passwd pwd;
      struct passwd *result = NULL;
      getpwuid_r(st.st_uid, &pwd, buffer, sizeof(buffer), &result);
      if (result != NULL) {
        strncpy(lastUsername, pwd.pw_name, NANO_OS_MAX_USERNAME_LENGTH);
      } else {
        strncpy(lastUsername, "UNKNOWN", NANO_OS_MAX_USERNAME_LENGTH);
      }
      lastUid = st.st_uid;
    }
    if (st.st_gid != lastGid) {
      struct group grp;
      struct group *result = NULL;
      getgrgid_r(st.st_gid, &grp, buffer, sizeof(buffer), &result);
      if (result != NULL) {
        strncpy(lastGroupname, grp.gr_name, NANO_OS_MAX_GROUPNAME_LENGTH);
      } else {
        strncpy(lastGroupname, "UNKNOWN", NANO_OS_MAX_GROUPNAME_LENGTH);
      }
      lastGid = st.st_gid;
    }

    // Parse the last-modified time
    gmtime_r(&st.st_mtime, &tm);

    // Print the full thing.  Format:
    // drwxrwxrwx <user> <group> <size> Day YYYY-MM-DD hh:mm:ss <entry name>[trailing /]
    mode_t mode = st.st_mode;
    snprintf(buffer, sizeof(buffer),
      "%c%c%c%c%c%c%c%c%c%c ",
      S_ISDIR(mode)    ? 'd' : '-',
      (mode & S_IRUSR) ? 'r' : '-',
      (mode & S_IWUSR) ? 'w' : '-',
      (mode & S_IXUSR) ? 'x' : '-',
      (mode & S_IRGRP) ? 'r' : '-',
      (mode & S_IWGRP) ? 'w' : '-',
      (mode & S_IXGRP) ? 'x' : '-',
      (mode & S_IROTH) ? 'r' : '-',
      (mode & S_IWOTH) ? 'w' : '-',
      (mode & S_IXOTH) ? 'x' : '-'
    );
    strcat(buffer, "%s %s %lld ");
    snprintf(&buffer[22], sizeof(buffer) - 22, "%s %d-%02d-%02d ",
      weekdays[tm.tm_wday],
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday
    );
    snprintf(&buffer[37], sizeof(buffer) - 37, "%02d:%02d:%02d ",
      tm.tm_hour, tm.tm_min, tm.tm_sec
    );
    strcat(buffer, "%s%s\n");
    printf(/* format= */ buffer,
      lastUsername,
      lastGroupname,
      (long long int) st.st_size,
      entry->d_name,
      (entry->d_type == DT_DIR) ? "/" : ""
    );
  }

  closedir(dirp);
}

int main(int argc, char **argv) {
  const char *path = getenv("PWD");
  if (argc > 1) {
    path = argv[1];
  }

  listDir(path);

  return 0;
}
