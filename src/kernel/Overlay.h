///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              09.01.2025
///
/// @file              Overlay.h
///
/// @brief             Definitions used for exporting functionality of overlays
///                    so that they're accessible from the kernel.
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

#ifndef OVERLAY_H
#define OVERLAY_H

// Standard includes.
#include "stdint.h"

// Headers from kernel space.
#include "BlockStorage.h"
#include "../user/NanoOsApi.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def NANO_OS_OVERLAY_MAGIC
///
/// @brief Value used to validate that an overlay header is valid.
#define NANO_OS_OVERLAY_MAGIC 0x4c4f734f6f6e614e // "NanoOsOL"

/// @def NANO_OS_OVERLAY_VERSION
///
/// The version of the overlay metadata, encoded into a 32-bit integer.
#define NANO_OS_OVERLAY_VERSION \
  ( (((uint32_t) 0) << 24) \
  | (((uint32_t) 0) << 16) \
  | (((uint32_t) 1) << 8) \
  | (((uint32_t) 0) << 0) \
  )

/// @def OVERLAY_EXT
///
/// @brief The file extension for overlay files.
#define OVERLAY_EXT ".overlay"

/// @def OVERLAY_EXT_LEN
///
/// @brief The number of characters in OVERLAY_EXT.
#define OVERLAY_EXT_LEN 8

/// @type OverlayFunction
///
/// @brief Function pointer for a function that can be called in an overlay from
/// the OS.
typedef void* (*OverlayFunction)(void*);

/// @struct NanoOsOverlayExport
///
/// @brief Definition for a single function exported from an overlay.
///
/// @param name The string name of the overlay function.
/// @param fn A pointer to the function within the overlay.
typedef struct NanoOsOverlayExport {
  const char name[16];
  OverlayFunction fn;
} NanoOsOverlayExport;

/// @struct NanoOsOverlayHeader
///
/// @brief The header used to export functionality within an overlay.
///
/// @param osApi A pointer to the NanoOsApi structure in the kernel that
///   manages all the standard C API interfaces.
/// @param env The array of NULL-terminated environment variables for the
///   running program.
/// @param overlay The FileBlockMetadata that points to the location of the
///   overlay on disk.
/// @param version The version of the overlay header.  Format will be:
///   (major << 24) | (minor << 16) | (revision << 8) | (build << 0).
/// @param magic The value of NANO_OS_OVERLAY_MAGIC to identify that this is a
///   valid NanoOs overlay header.  This parameter must come last.
typedef struct NanoOsOverlayHeader {
  NanoOsApi *osApi;
  char **env;
  FileBlockMetadata overlay;
  uint32_t version;
  uint64_t magic;
} NanoOsOverlayHeader;

/// @struct NanoOsOverlayMap
///
/// @brief The map of exported information from an overlay.
///
/// @param header An embedded NanoOsOverlayHeader structure.
/// @param exports Apointer to an array of NanoOsOverlayExport items exported by
///   an overlay.
typedef struct NanoOsOverlayMap {
  NanoOsOverlayHeader header;
  NanoOsOverlayExport *exports;
  uint16_t numExports;
} NanoOsOverlayMap;

/// @struct MainArgs
///
/// @brief Structure to hold the standard arc/argv arguments to a C main
/// function.
///
/// @param argc The value of argc to be passed to a main function.
/// @param argv The value of argv to be passed to a main function.
typedef struct MainArgs {
  int argc;
  char **argv;
} MainArgs;

#ifdef __cplusplus
}
#endif

#endif // OVERLAY_H

