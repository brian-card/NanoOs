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

#include "string.h"
#include "NanoOsLibC.h"
#include "NanoOsSysUtsname.h"
#include "../kernel/NanoOs.h"
#include "NanoOsUnistd.h"

/// @fn int nanoOsUname(struct utsname *buf)
///
/// @brief Get information about the system.
///
/// @param buf A pointer to a struct utsname to be populated.
///
/// @return Returns 0 on success, -1 on failure.  On failure, the value of
/// errno is also set.
int nanoOsUname(struct utsname *buf) {
  if (buf == NULL) {
    errno = EFAULT;
    return -1;
  }
  
  strncpy(buf->sysname, "NanoOs", sizeof(buf->sysname));
  nanoOsGethostname(buf->nodename, sizeof(buf->nodename));
  strncpy(buf->release, "0.2.0", sizeof(buf->release));
  strncpy(buf->version, "", sizeof(buf->version));
  strncpy(buf->machine, "arm", sizeof(buf->machine));
  
  return 0;
}

