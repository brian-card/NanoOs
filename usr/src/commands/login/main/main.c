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

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#undef tcgetattr
#undef tcsetattr

/// @def SHELL_PATH
///
/// @brief The absolute path to the shell program on the filesystem.
#define SHELL_PATH "/usr/bin/mush"

/// @def SHELL_NAME
///
/// @brief The desired name to show for the shell program when we exec it.
/// This will be used as argv[0] in the args we pass in to execve.
#define SHELL_NAME "mush"

int main(int argc, char **argv) {
  int returnValue = 0;
  
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <username>\n", argv[0]);
    return 1;
  }
  const char *username = argv[1];
  
  char *passwdStringBuffer = (char*) malloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
  if (passwdStringBuffer == NULL) {
    fprintf(stderr,
      "ERROR! Could not allocate space for passwdStringBuffer in %s.\n",
      argv[0]);
    return 1;
  }
  
  struct passwd *pwd = (struct passwd*) malloc(sizeof(struct passwd));
  if (pwd == NULL) {
    fprintf(stderr,
      "ERROR! Could not allocate space for pwd in %s.\n", argv[0]);
    returnValue = 1;
    goto freePasswdStringBuffer;
  }
  
  struct passwd *result = NULL;
  returnValue = getpwnam_r(username, pwd,
    passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
  if (returnValue != 0) {
    fprintf(stderr, "getpwnam_r returned status %d\n", returnValue);
    returnValue = 1;
    goto freePwd;
  } else if (result == NULL) {
    // returnValue is 0 but the result passwd struct is NULL.  This means that
    // the function completed successfully but that there's no such user.  This
    // is not an error.  We don't want to give any indication that the username
    // wasn't found because that's a security vulnerability.  Clear out the
    // password that the rest of the function will use in its logic and
    // continue.
    *passwdStringBuffer = '\0';
    pwd->pw_passwd = passwdStringBuffer;
  }
  
  char *userPassword = (char*) malloc(NANO_OS_MAX_PASSWORD_LENGTH + 1);
  if (userPassword == NULL) {
    fprintf(stderr,
      "ERROR! Could not allocate space for userPassword in %s.\n",
      argv[0]);
    returnValue = 1;
    goto freePwd;
  }
  *userPassword = '\0';
  
  struct termios *old = (struct termios*) malloc(sizeof(struct termios));
  if (old == NULL) {
    fprintf(stderr, "ERROR! Could not allocate space for old in %s.\n",
      argv[0]);
    returnValue = 1;
    goto freeUserPassword;
  }
  
  struct termios *new = (struct termios*) malloc(sizeof(struct termios));
  if (new == NULL) {
    fprintf(stderr, "ERROR! Could not allocate space for new in %s.\n",
      argv[0]);
    returnValue = 1;
    goto freeOld;
  }
  
  fputs("password: ", stdout);
  
  // Disable echo
  overlayMap.header.osApi->tcgetattr(STDIN_FILENO, old);
  *new = *old;
  new->c_lflag &= ~ECHO;
  overlayMap.header.osApi->tcsetattr(STDIN_FILENO, TCSANOW, new);
  
  char *input = fgets(userPassword, NANO_OS_MAX_PASSWORD_LENGTH + 1, stdin);
  
  // Restore echo
  overlayMap.header.osApi->tcsetattr(STDIN_FILENO, TCSANOW, old);
  
  // Print a newline since one didn't get echoed when the user hit <ENTER>.
  fputs("\n", stdout);
  
  if ((input != NULL) && (strlen(input) > 0)
    && (input[strlen(input) - 1] == '\n')
  ) {
    input[strlen(input) - 1] = '\0';
  }
  
  if (strcmp(userPassword, pwd->pw_passwd) == 0) {
    fputs("Login successful!\n", stderr);
  } else {
    fputs("Login failed!\n", stderr);
    fprintf(stderr, "Expected \"%s\", got \"%s\"\n", argv[1], userPassword);
    returnValue = 1;
    goto freeNew;
  }
  
  if (setuid(pwd->pw_uid) != 0) {
    fputs("ERROR:  Could not set the user ID of the process.\n", stderr);
  }
  
  // The login succeeded, so exec the shell rather than exiting.
  char *shellArgv[] = {
    SHELL_NAME,
    NULL,
  };
  // Maximum user home directory is strlen("/home/") + LOGIN_NAME_MAX.
  // Environment variable will be strlen("HOME=") plus the above.
  char envHome[LOGIN_NAME_MAX + 11];
  strcpy(envHome, "HOME=/home/");
  strcat(envHome, username);
  
  // The PWD variable will be one less than HOME since it's only 3 characters.
  char envPwd[LOGIN_NAME_MAX + 10];
  strcpy(envPwd, "PWD=/home/");
  strcat(envPwd, username);
  
  char *shellEnvp[] = {
    envHome,
    envPwd,
    "PATH=/usr/bin",
    NULL,
  };
  
  execve(SHELL_PATH, shellArgv, shellEnvp);
  // If we get here then the exec failed.  We don't really have to check the
  // return value but we do need to print out what happened as documented by
  // errno.
  fputs("ERROR! execve failed with status: ", stderr);
  fputs(strerror(errno), stderr);
  fputs("\n", stderr);
  
freeNew:
  free(new);

freeOld:
  free(old);

freeUserPassword:
  free(userPassword);

freePwd:
  free(pwd);

freePasswdStringBuffer:
  free(passwdStringBuffer);

  // Exit.  This will cause the getty program to be reloaded.
  return returnValue;
}

