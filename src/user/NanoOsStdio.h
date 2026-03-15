///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.22.2025
///
/// @file              NanoOsStdio.h
///
/// @brief             Definitions stdio.h functionality for NanoOs.
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

#ifndef NANO_OS_USER_STDIO_H
#define NANO_OS_USER_STDIO_H

#undef FILE

// Standard C includes
#include <stdarg.h>
#include <stddef.h>

// NanoOs includes
#include "NanoOsDebug.h"

#define FILE C_FILE
//// #include "stdio.h"
#undef FILE

#define FILE NanoOsFile

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct NanoOsFile NanoOsFile;

#ifdef stdin
#undef stdin
#endif // stdin
#define stdin nanoOsStdin

#ifdef stdout
#undef stdout
#endif // stdout
#define stdout nanoOsStdout

#ifdef stderr
#undef stderr
#endif // stderr
#define stderr nanoOsStderr

#define EOF (-1)

extern FILE *nanoOsStdin;
extern FILE *nanoOsStdout;
extern FILE *nanoOsStderr;

// Debug functions
int printString_(const char *string);
#define printString(str) printString_((const char*) (str))
int printInt_(long long int integer);
#define printInt(value) printInt_((long long int) (value))
int printDouble(double floatingPointValue);
int printHex_(unsigned long long int integer);
#define printHex(integer) printHex_((unsigned long long int) (integer))
int printList_(const char *firstString, ...);
#define printList(firstString, ...) printList_(firstString, ##__VA_ARGS__, STOP)

// C-like functions
int vsscanf(const char *buffer, const char *format, va_list args);
int sscanf(const char *buffer, const char *format, ...);

// Exported IO functions
int nanoOsFPuts(const char *s, FILE *stream);
#ifdef fputs
#undef fputs
#endif
#define fputs nanoOsFPuts

#ifdef puts
#undef puts
#endif
#define puts(s) nanoOsFPuts(s, stdout) nanoOsFPuts("\n", stdout)

int nanoOsVFPrintf(FILE *stream, const char *format, va_list args);
#ifdef vfprintf
#undef vfprintf
#endif
#define vfprintf nanoOsVFPrintf

int nanoOsFPrintf(FILE *stream, const char *format, ...);
#ifdef fprintf
#undef fprintf
#endif
#define fprintf nanoOsFPrintf

int nanoOsPrintf(const char *format, ...);
#ifdef printf
#undef printf
#endif
#define printf nanoOsPrintf

char *nanoOsFGets(char *buffer, int size, FILE *stream);
#ifdef fgets
#undef fgets
#endif
#define fgets nanoOsFGets

int nanoOsVFScanf(FILE *stream, const char *format, va_list ap);
#ifdef vfscanf
#undef vfscanf
#endif
#define vfscanf nanoOsVFScanf

int nanoOsFScanf(FILE *stream, const char *format, ...);
#ifdef fscanf
#undef fscanf
#endif
#define fscanf nanoOsFScanf

int nanoOsScanf(const char *format, ...);
#ifdef scanf
#undef scanf
#endif
#define scanf nanoOsScanf

int nanoOsFileno(FILE *stream);
#ifdef fileno
#undef fileno
#endif
#define fileno nanoOsFileno

size_t nanoOsFread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t nanoOsFwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_USER_STDIO_H

#include "../kernel/Filesystem.h"
