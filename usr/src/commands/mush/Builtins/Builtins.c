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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define printDebug(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)

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
  printDebug("Evaluating builtin \"%s\"\n", input);
  
  void *returnValue = (void*) ((intptr_t) 0); // Default to good status.
  if (strcmp(input, "exit") == 0) {
    returnValue = (void*) ((intptr_t) -1);
  } else if (strcmp(input, "pwd") == 0) {
    fputs(getenv("PWD"), stdout);
    fputs("\n", stdout);
  } else {
    returnValue = (void*) ((intptr_t) -2);
  }
  
  return returnValue;
}

