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

static inline int strncmp(const char *s1, const char *s2, size_t n) {
  return overlayMap.header.osApi->strncmp(s1, s2, n);
}
#define strcmp(s1, s2) strncmp(s1, s2, (size_t) -1)
static inline size_t strlen(const char *s) {
  return overlayMap.header.osApi->strlen(s);
}

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_LIB_STRING_H

