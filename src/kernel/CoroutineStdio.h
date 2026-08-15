///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              08.14.2026
///
/// @file              CoroutineStdio.h
///
/// @brief             Definitions for using stdio calls from the Coroutines.c
///                    library within NanoOs.
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

#ifndef COROUTINE_STDIO_H
#define COROUTINE_STDIO_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct NanoOsFile NanoOsFile;
int nanoOsFprintf(NanoOsFile *stream, const char *format, ...);
#ifdef fprintf
#undef fprintf
#endif
#define fprintf nanoOsFprintf

#ifdef stderr
#undef stderr
#endif // stderr
#define stderr nanoOsStderr
extern NanoOsFile *nanoOsStderr;

// Raw print functions
int printChar_(char character);
#define printChar(character) printChar_((char) (character))
int printString_(const char *string);
#define printString(str) printString_((const char*) (str))
int printInt_(long long int integer);
#define printInt(value) printInt_((long long int) (value))
int printDouble(double floatingPointValue);
int printHex_(unsigned long long int integer);
#define printHex(integer) printHex_((unsigned long long int) (integer))

#ifdef __cplusplus
}
#endif

#endif // COROUTINE_STDIO_H

