///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              09.05.2025
///
/// @file              string.h
///
/// @brief             Definitions used for accessing functionality in the C
///                    string library.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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

#ifndef STRING_H
#define STRING_H

#include "NanoOsUser.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Copying functions:
static inline void *memcpy(void *dest, const void *src, size_t n) {
  return overlayMap.header.osApi->memcpy(dest, src, n);
}
static inline void *memmove(void *dest, const void *src, size_t n) {
  return overlayMap.header.osApi->memmove(dest, src, n);
}
static inline char *strcpy(char *dst, const char *src) {
  return overlayMap.header.osApi->strcpy(dst, src);
}
static inline char *strncpy(char *dst, const char *src, size_t dsize) {
  return overlayMap.header.osApi->strncpy(dst, src, dsize);
}
static inline char *strcat(char *restrict dst, const char *restrict src) {
  return overlayMap.header.osApi->strcat(dst, src);
}
static inline char *strncat(char *dst, const char *src, size_t ssize) {
  return overlayMap.header.osApi->strncat(dst, src, ssize);
}

// Search functions:
static inline int memcmp(const void *s1, const void *s2, size_t n) {
  return overlayMap.header.osApi->memcmp(s1, s2, n);
}
static inline int strcmp(const char *s1, const char *s2) {
  return overlayMap.header.osApi->strcmp(s1, s2);
}
static inline int strncmp(const char *s1, const char *s2, size_t n) {
  return overlayMap.header.osApi->strncmp(s1, s2, n);
}
static inline char *strstr(const char *haystack, const char *needle) {
  return overlayMap.header.osApi->strstr(haystack, needle);
}
static inline char *strchr(const char *s, int c) {
  return overlayMap.header.osApi->strchr(s, c);
}
static inline char *strrchr(const char *s, int c) {
  return overlayMap.header.osApi->strrchr(s, c);
}
static inline size_t strspn(const char *s, const char *accept) {
  return overlayMap.header.osApi->strspn(s, accept);
}
static inline size_t strcspn(const char *s, const char *reject) {
  return overlayMap.header.osApi->strcspn(s, reject);
}

// Miscellaaneous string functions:
static inline void *memset(void *s, int c, size_t n) {
  return overlayMap.header.osApi->memset(s, c, n);
}
static inline char *strerror(int errnum) {
  return overlayMap.header.osApi->strerror(errnum);
}
static inline size_t strlen(const char *s) {
  return overlayMap.header.osApi->strlen(s);
}

#ifdef __cplusplus
}
#endif

#endif // STRING_H

