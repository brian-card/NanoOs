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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>


/// @fn size_t pathCollapse(char *path)
///
/// @brief Collapse a full path that contains relative directories into the
/// complete absolute path without them.
///
/// @param path The full path that may or may not contain relative directories.
///   This parameter is modified in place.
///
/// @return Returns the length of the new path on success, 0 on error.
size_t pathCollapse(char *path) {
  if ((path == NULL) || (*path != '/')) {
    return 0;
  }

  size_t writeIndex = 1; // path[0] is always the leading '/'

  const char *readPointer = path + 1;
  while (*readPointer == '/') {
    // Skip all the leading '/' characters
    readPointer++;
  }

  while (*readPointer != '\0') {
    const char *directoryEnd = readPointer;
    while ((*directoryEnd != '\0') && (*directoryEnd != '/')) {
      directoryEnd++;
    }
    size_t directoryLength = (size_t) (directoryEnd - readPointer);

    if ((directoryLength == 2)
      && (readPointer[0] == '.') && (readPointer[1] == '.')
    ) {
      // Parent directory; erase the last written directory name and its '/'
      while ((writeIndex > 1) && (path[writeIndex - 1] != '/')) {
        writeIndex--;
      }
      if (writeIndex > 1) {
        writeIndex--;
      }
    } else if ((directoryLength > 1) || (readPointer[0] != '.')) {
      // Actual directory name; copy it
      if (writeIndex > 1) {
        path[writeIndex++] = '/';
      }
      // Ranges may overlap, so use memmove instead of memcpy
      memmove(path + writeIndex, readPointer, directoryLength);
      writeIndex += directoryLength;
    } // else this is just a '.' current directory reference; skip it
    readPointer = directoryEnd;

    while (*readPointer == '/') {
      // collapse "//"
      readPointer++;
    }
  }

  path[writeIndex] = '\0';
  return writeIndex;
}

/// @fn void* processBuiltin(void *args)
///
/// @brief Process commands built into the MUSH shell.
///
/// @param args A pointer to a C string, cast to a void*.
///
/// @return Returns the integer return value of the built-in command, cast to a
/// void*.  A return value of -1 indicates that the shell should exit.  A
/// return value of -2 indicates that the command was not found.  Any
/// non-negative return value is the return value from the command that was
/// run.
void* processBuiltin(void *args) {
  char *input = (char*) args;
  printDebugString("Evaluating builtin ");
  printDebugString(input);
  printDebugString("\n");
  
  void *returnValue = (void*) ((intptr_t) 0); // Default to good status.
  if (strcmp(input, "pwd") == 0) {
    fputs(getenv("PWD"), stdout);
    fputs("\n", stdout);
  } else if ((strncmp(input, "cd", 2) == 0)
    && ((input[2] == '\0') || (input[2] == ' ') || (input[2] == '\t'))
  ) {
    input = &input[2];
    input = &input[strspn(input, " \t")];
    char *end = &input[strcspn(input, " \t")];
    *end = '\0';
    
    if (*input == '\0') {
      input = getenv("HOME");
    } else if (strcmp(input, "-") == 0) {
      input = getenv("OLDPWD");
    }
    if (input == NULL) {
      return returnValue;
    }
    
    char *path = (char*) malloc(96);
    if (path == NULL) {
      errno = ENOMEM;
      return (void*) ((intptr_t) -3);
    }
    
    if (*input == '/') {
      strcpy(path, input);
    } else {
      snprintf(path, 96, "%s/%s", getenv("PWD"), input);
    }
    pathCollapse(path);
    setenv("OLDPWD", getenv("PWD"), true);
    setenv("PWD", path, true);
    
    free(path); path = NULL;
  } else {
    returnValue = (void*) ((intptr_t) -2);
  }
  
  return returnValue;
}

