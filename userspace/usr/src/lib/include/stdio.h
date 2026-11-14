///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.15.2026
///
/// @file              NanoOsDebug.h
///
/// @brief             Definitions used for accessing functionality in the C
///                    stdio library.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
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
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef STDIO_H
#define STDIO_H

#include "NanoOsUser.h"

#ifdef __cplusplus
extern "C"
{
#endif

// C defines:
#define EOF (-1)

// Standard streams:
#define stdin \
  overlayMap.header.osApi->stdin
#define stdout \
  overlayMap.header.osApi->stdout
#define stderr \
  overlayMap.header.osApi->stderr

// File operations:
static inline FILE* fopen(const char *pathname, const char *mode) {
  return overlayMap.header.osApi->fopen(pathname, mode);
}
static inline int fclose(FILE *stream) {
  return overlayMap.header.osApi->fclose(stream);
}
static inline int remove(const char *pathname) {
  return overlayMap.header.osApi->remove(pathname);
}
static inline int fseek(FILE *stream, long offset, int whence) {
  return overlayMap.header.osApi->fseek(stream, offset, whence);
}
static inline int fileno(FILE *stream) {
  return overlayMap.header.osApi->fileno(stream);
}

// Formatted I/O:
static inline int vsscanf(const char *str, const char *format, va_list ap) {
  return overlayMap.header.osApi->vsscanf(str, format, ap);
}
static inline int sscanf(const char *str, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vsscanf(str, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int vfscanf(FILE *stream, const char *format, va_list ap) {
  return overlayMap.header.osApi->vfscanf(stream, format, ap);
}
static inline int fscanf(FILE *stream, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vfscanf(stream, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int scanf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vfscanf(stdin, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int vfprintf(FILE *stream, const char *format, va_list ap) {
  return overlayMap.header.osApi->vfprintf(stream, format, ap);
}
static inline int fprintf(FILE *stream, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vfprintf(stream, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int printf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vfprintf(stdout, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int vsnprintf(char *str, size_t size,
  const char *format, va_list ap
) {
  return overlayMap.header.osApi->vsnprintf(str, size, format, ap);
}
static inline int vsprintf(char *str, const char *format, va_list ap) {
  return overlayMap.header.osApi->vsnprintf(str, (size_t) -1, format, ap);
}
static inline int snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue = overlayMap.header.osApi->vsnprintf(str, size, format, ap);
  va_end(ap);
  return returnValue;
}
static inline int sprintf(char *str, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int returnValue
    = overlayMap.header.osApi->vsnprintf(str, (size_t) -1, format, ap);
  va_end(ap);
  return returnValue;
}

// Character I/O:
static inline int fputs(const char *s, FILE *stream) {
  int returnValue = 0;
  size_t len = overlayMap.header.osApi->strlen(s);
  if (overlayMap.header.osApi->fwrite(s, 1, len, stream) != len) {
    returnValue = EOF;
  }
  return returnValue;
}
static inline int puts(const char *s) {
  if (fputs(s, stdout) != 0) {
    return EOF;
  }
  return fputs("\n", stdout);
}
static inline char *fgets(char *s, int size, FILE *stream) {
  char *returnValue = NULL;
  if (size > 0) {
    size_t bytesRead = overlayMap.header.osApi->fread(s, 1, size - 1, stream);
    if (bytesRead > 0) {
      s[bytesRead] = '\0';
      returnValue = s;
    }
  }
  return returnValue;
}

// Direct I/O:
static inline size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return overlayMap.header.osApi->fread(ptr, size, nmemb, stream);
}
static inline size_t fwrite(
  void *ptr, size_t size, size_t nmemb, FILE *stream
) {
  return overlayMap.header.osApi->fwrite(ptr, size, nmemb, stream);
}

#ifdef __cplusplus
}
#endif

#endif // STDIO_H

