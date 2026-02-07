///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              02.07.2026
///
/// @file              libstring.h
///
/// @brief             Library header that can be included locally for userspace
///                    libraries to provide the functionality of string.h.
///
/// @copyright
///                   Copyright (c) 2012-2026 James Card
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

#ifndef NANO_OS_LIB_STRING_H
#define NANO_OS_LIB_STRING_H

#ifdef __cplusplus
extern "C"
{
#endif

static size_t libStrlen(const char *str) {
  size_t len = 0;
  if (str != NULL) {
    for (; str[len] != '\0'; len++);
  }
  
  return len;
}

static int libStrncmp(const char *s1, const char *s2, size_t n) {
  int returnValue = 0;
  
  if (s1 == NULL) {
    returnValue--;
  }
  if (s2 == NULL) {
    returnValue++;
  }
  if (returnValue != 0) {
    return returnValue;
  }
  
  for (size_t ii = 0; (ii < n) && (returnValue == 0); ii++) {
    int c1 = (int) s1[ii];
    int c2 = (int) s2[ii];
    returnValue = c1 - c2;
    if ((c1 == 0) || (c2 == 0)) {
      break;
    }
  }
  
  return returnValue;
}

#define libStrcmp(s1, s2) strncmp(s1, s2, (size_t) -1)

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_LIB_STRING_H

