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

/// @file main.c
///
/// @brief Implementation of /system/commands/passwd, which changes a password
/// in /secure/shadow.  It runs only through POSIX_SPAWN_SYSTEM, at executive
/// level as the user who ran /usr/bin/passwd.

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"

#undef tcgetattr
#undef tcsetattr

/// @def SHADOW_PATH
///
/// @brief The full path of the password checksum file.
#define SHADOW_PATH "/secure/shadow"

/// @def SHADOW_NEW_PATH
///
/// @brief The full path the new password checksum file is written to before
/// it replaces the old one.
#define SHADOW_NEW_PATH "/secure/shadow.new"

/// @def SHADOW_LOCK_PATH
///
/// @brief The full path of the directory that passwd holds while it rewrites
/// the password checksum file.
#define SHADOW_LOCK_PATH "/secure/shadow.lock"

/// @def SHADOW_LINE_SIZE
///
/// @brief The size of the buffer that the password checksum file is copied
/// through.  Longer lines are copied in pieces.
#define SHADOW_LINE_SIZE 64

/// @def RETRIES
///
/// @brief How many times to try to take the lock or replace the file, a
/// second apart.
#define RETRIES 5

/// @def ROOT_UID
///
/// @brief The user ID that may change any user's password.
#define ROOT_UID 0

/// @fn void printUsage(FILE *stream)
///
/// @brief Print the usage summary for passwd.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs("Usage: passwd [user name]\n", stream);
}

/// @fn bool readPassword(const char *prompt, char *password)
///
/// @brief Prompt for a password without echoing it.
///
/// @param prompt The prompt to print.
/// @param password Where to store the password, which must hold
///   NANO_OS_MAX_PASSWORD_LENGTH + 1 bytes.
///
/// @return Returns true if a line was read, false if not.
static bool readPassword(const char *prompt, char *password) {
  struct termios old;
  struct termios quiet;
  fputs(prompt, stdout);
  tcgetattr(STDIN_FILENO, &old);
  quiet = old;
  quiet.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
  char *input = fgets(password, NANO_OS_MAX_PASSWORD_LENGTH + 1, stdin);
  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  fputs("\n", stdout);
  if (input == NULL) {
    return false;
  }
  password[strcspn(password, "\n")] = '\0';
  return true;
}

/// @fn bool shadowLineIsUser(const char *line, const char *username)
///
/// @brief Determine whether a line of the password checksum file belongs to
/// a user.
///
/// @param line The start of the line.
/// @param username The user's name.
///
/// @return Returns true if the line is the user's, false if not.
static bool shadowLineIsUser(const char *line, const char *username) {
  size_t length = strlen(username);
  return (strncmp(line, username, length) == 0) && (line[length] == ':');
}

/// @fn static bool shadowLookup(const char *username, char *line, 
///   long int *checksum)
///
/// @brief Find a user's checksum in the password checksum file.
///
/// @param username The user's name.
/// @param line A buffer of SHADOW_LINE_SIZE bytes to read through.
/// @param checksum Where to store the checksum.
///
/// @return Returns true if the user has a line, false if not.
static bool shadowLookup(const char *username, char *line, long int *checksum)
{
  FILE *file = fopenFullPath(SHADOW_PATH, "r");
  bool found = false;
  bool atLineStart = true;
  while ((file != NULL) && !found
    && (fgets(line, SHADOW_LINE_SIZE, file) != NULL)
  ) {
    if (atLineStart && shadowLineIsUser(line, username)) {
      *checksum = strtol(&line[strlen(username) + 1], NULL, 10);
      found = true;
    }
    atLineStart = (strchr(line, '\n') != NULL);
  }
  if (file != NULL) {
    fclose(file);
  }
  return found;
}

/// @fn bool shadowWriteNew(const char *username, long int checksum,
///   char *line)
///
/// @brief Write the new password checksum file:  a copy of the old one with
/// the user's line replaced, or added if it had none.
///
/// @param username The user's name.
/// @param checksum The user's new checksum.
/// @param line A buffer of SHADOW_LINE_SIZE bytes to copy through.
///
/// @return Returns true if the whole file was written, false if not.
static bool shadowWriteNew(const char *username, long int checksum,
  char *line
) {
  FILE *out = fopenFullPath(SHADOW_NEW_PATH, "w");
  if (out == NULL) {
    return false;
  }
  FILE *in = fopenFullPath(SHADOW_PATH, "r");
  bool written = true;
  bool replaced = false;
  bool skipping = false;
  bool atLineStart = true;
  while ((in != NULL) && (fgets(line, SHADOW_LINE_SIZE, in) != NULL)) {
    if (atLineStart) {
      skipping = shadowLineIsUser(line, username);
      if (skipping && !replaced) {
        written &= (fprintf(out, "%s:%ld\n", username, checksum) > 0);
        replaced = true;
      }
    }
    if (!skipping) {
      written &= (fputs(line, out) != EOF);
    }
    atLineStart = (strchr(line, '\n') != NULL);
  }
  if (!replaced) {
    written &= (fprintf(out, atLineStart ? "%s:%ld\n" : "\n%s:%ld\n",
      username, checksum) > 0);
  }
  if (in != NULL) {
    fclose(in);
  }
  written &= (fclose(out) == 0);
  return written;
}

/// @fn const char* shadowUpdate(const char *username, long int checksum,
///   char *line)
///
/// @brief Replace a user's line in the password checksum file, holding the
/// lock while the file is rewritten.
///
/// @param username The user's name.
/// @param checksum The user's new checksum.
/// @param line A buffer of SHADOW_LINE_SIZE bytes to copy through.
///
/// @return Returns NULL on success or the message to print on failure.
static const char* shadowUpdate(const char *username, long int checksum,
  char *line
) {
  int tries = 0;
  while (mkdirFullPath(SHADOW_LOCK_PATH, 0) != 0) {
    if ((errno != EEXIST) || (++tries >= RETRIES)) {
      return "passwd: the password file is busy\n";
    }
    sleep(1);
  }

  const char *message = "passwd: could not write the password file\n";
  if (shadowWriteNew(username, checksum, line)) {
    // A login reading the file keeps it from being replaced for a moment.
    for (tries = 0; (message != NULL) && (tries < RETRIES); tries++) {
      if (renameFullPath(SHADOW_NEW_PATH, SHADOW_PATH) == 0) {
        message = NULL;
      } else {
        sleep(1);
      }
    }
  }
  rmdirFullPath(SHADOW_LOCK_PATH);
  return message;
}

/// @fn const char* checkCaller(char **argv, struct passwd *pwd, char *buffer,
///   char *password, char *line)
///
/// @brief Decide whose password changes and check the caller's password if
/// the caller isn't root.
///
/// @param argv The arguments passwd was run with.
/// @param pwd Where to store the target user's passwd entry.
/// @param buffer The string buffer for pwd, of NANO_OS_PASSWD_STRING_BUF_SIZE
///   bytes.
/// @param password A buffer of NANO_OS_MAX_PASSWORD_LENGTH + 1 bytes.
/// @param line A buffer of SHADOW_LINE_SIZE bytes.
///
/// @return Returns NULL if the password may be changed or the message to
/// print if not.
static const char* checkCaller(char **argv, struct passwd *pwd, char *buffer,
  char *password, char *line
) {
  struct passwd *result = NULL;
  uid_t uid = getuid();
  if ((getpwuid_r(uid, pwd, buffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result)
      != 0) || (result == NULL)
  ) {
    return "passwd: you are not a known user\n";
  }

  if (uid == ROOT_UID) {
    if ((argv[1] != NULL) && ((getpwnam_r(argv[1], pwd, buffer,
        NANO_OS_PASSWD_STRING_BUF_SIZE, &result) != 0) || (result == NULL))
    ) {
      return "passwd: no such user\n";
    }
    return NULL;
  }

  if ((argv[1] != NULL) && (strcmp(argv[1], pwd->pw_name) != 0)) {
    return "passwd: only root can change another user's password\n";
  }
  long int checksum = 0;
  if (!readPassword("Current password: ", password)
    || !shadowLookup(pwd->pw_name, line, &checksum)
    || (passwordChecksum(pwd->pw_name, password) != checksum)
  ) {
    sleep(3);
    return "passwd: password incorrect\n";
  }
  return NULL;
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if (argc > 2) {
    printUsage(stderr);
    return 1;
  }

  struct passwd *pwd = (struct passwd*) malloc(sizeof(struct passwd));
  char *buffer = (char*) malloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
  char *password = (char*) malloc(NANO_OS_MAX_PASSWORD_LENGTH + 1);
  char *again = (char*) malloc(NANO_OS_MAX_PASSWORD_LENGTH + 1);
  char *line = (char*) malloc(SHADOW_LINE_SIZE);
  const char *message = "passwd: out of memory\n";
  if ((pwd != NULL) && (buffer != NULL) && (password != NULL)
    && (again != NULL) && (line != NULL)
  ) {
    message = checkCaller(argv, pwd, buffer, password, line);
  }
  if ((message == NULL)
    && (!readPassword("New password: ", password)
      || !readPassword("Retype new password: ", again)
      || (strcmp(password, again) != 0))
  ) {
    message = "passwd: the passwords don't match\n";
  } else if ((message == NULL) && (*password == '\0')) {
    message = "passwd: password unchanged\n";
  }
  if (message == NULL) {
    message = shadowUpdate(pwd->pw_name,
      passwordChecksum(pwd->pw_name, password), line);
  }

  if (message == NULL) {
    fputs("passwd: password updated\n", stdout);
  } else {
    fputs(message, stderr);
  }
  free(line);
  free(again);
  free(password);
  free(buffer);
  free(pwd);
  return (message == NULL) ? 0 : 1;
}
