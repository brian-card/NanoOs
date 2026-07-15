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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              Link.c
///
/// @brief             Soft link support for filesystems that don't natively
///                    support soft links.
///
///////////////////////////////////////////////////////////////////////////////

#include "stddef.h"
#include "stdint.h"
#include "string.h"

#if defined(__linux__) || defined(__linux) || defined(_WIN32)

// We're compiling as an application on another OS.
#include "stdio.h"
#include "stdlib.h"
#include "errno.h"
#define MAX_PATH_LENGTH 255
#define MIN(x, y) (((x) <= (y)) ? (x) : (y))

#else

// We're compiling from within NanoOs.
#include "Filesystem.h"
#include "MemoryManager.h"
#include "NanoOs.h"
#include "../user/NanoOsLibC.h"

#endif // OS-specific imports

// KEEP_IN_FLASH (defined in NanoOs.h) isn't available when this file is
// compiled as a standalone host tool.  It's only needed to keep string data
// out of .rodata on targets where that section is removed from the final
// binary, which doesn't apply to a host build, so fall back to a no-op.
#ifndef KEEP_IN_FLASH
#define KEEP_IN_FLASH
#endif

// Include our own header.
#include "Link.h"

/// @enum LinkValueType
///
/// @brief Type values used in link TLV metadata.
typedef enum LinkValueType {
  LINK_VALUE_TYPE_INVALID = 0,
  LINK_VALUE_TYPE_PATH,
  NUM_LINK_VALUE_TYPES
} LinkValueType;

/// @var _linkMagicString
///
/// @brief 8-byte magic string at the beginning of a link to designate it as
/// a NanoOs link.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _linkMagicString[] KEEP_IN_FLASH = "SoftLink";

/// @def LINK_MAGIC
///
/// @brief Magic value at the begining of a link to designate it as a NanoOS
/// link.
#define LINK_MAGIC ((uint64_t*) _linkMagicString)

/// @brief LINK_MAGIC_SIZE
///
/// @brief Size, in bytes, of the magic value of the beginning of a NanoOs link.
#define LINK_MAGIC_SIZE (sizeof(uint64_t))

/// @def LINK_TYPE_LENGTH_SIZE
///
/// @brief Size, in bytes, of type + length metadata for a value.
#define LINK_TYPE_LENGTH_SIZE 4

/// @def LINK_CHECKSUM_SIZE
///
/// @brief Size, in bytes, of a checksum for a value.
#define LINK_CHECKSUM_SIZE 2

/// @def LINK_MAGIC_INDEX
///
/// @brief The index for the LINK_MAGIC in a link header.  This is an 8-byte
/// field.
#define LINK_MAGIC_INDEX 0

/// @def LINK_HEADER_SIZE_INDEX
///
/// @brief The index within the header of the header length.  This is a 2-byte
/// field.
#define LINK_HEADER_SIZE_INDEX 8

/// @def LINK_VERSION_INDEX
///
/// @brief The index within the header of the link version number.  This is a
/// 2-byte field in version 1.
#define LINK_VERSION_INDEX 10

/// @def LINK_VERSION1_HEADER_SIZE
///
/// @brief The total size, in bytes, of the link header in version 1.
#define LINK_VERSION1_HEADER_SIZE 12

/// @def LINK_VERSION1_PATH_TYPE_INDEX
///
/// @brief The index within the link file of the path type value.  This is a
/// 2-byte field in version 1.
#define LINK_VERSION1_PATH_TYPE_INDEX 12

/// @def LINK_VERSION1_PATH_LENGTH_INDEX
///
/// @brief The index within the link file of the of the path length.  This is a
/// 2-byte field in version 1.
#define LINK_VERSION1_PATH_LENGTH_INDEX 14

/// @def LINK_VERSION1_PATH_INDEX
///
/// @brief The index within the link file of the target path.
#define LINK_VERSION1_PATH_INDEX 16

/// @def MAX_LINK_SIZE
///
/// @brief The maximum size of a valid link file.
#define MAX_LINK_SIZE \
  (LINK_VERSION1_PATH_INDEX + LINK_TYPE_LENGTH_SIZE \
    + MAX_PATH_LENGTH + LINK_CHECKSUM_SIZE)

/// @fn static char* getFilename(const char *path)
///
/// @brief Helper function to get filename from path.
///
/// @param path Full path to a file.
///
/// @return Returns just the filename portion of the path.
const char* getFilename(const char *path) {
  const char *lastSlashAt = strrchr(path, '/');
  const char *filename = path;
  
  if (lastSlashAt != NULL) {
    filename = lastSlashAt + 1;
  }
  
  return filename;
}

/// @var _writeBinaryMode
///
/// @brief fopen() mode string used to create a new link file.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _writeBinaryMode[] KEEP_IN_FLASH = "wb";

/// @fn int makeLink(const char *target, const char *linkFile)
///
/// @brief Make a link to a source file on the filesystem from a specified
/// location.
///
/// @param target The target of the link.
/// @param linkFile The path to the new link on the filesystem.
///
/// @return Returns 0 on success, -1 on error.
int makeLink(const char *target, const char *linkFile) {
  if (target == NULL) {
    return -1;
  }
  
  // Determine output filename
  char *outputPath = NULL;
  size_t linkFileLength = 0;
  if (linkFile != NULL) {
    linkFileLength = strlen(linkFile);
  }
  
  if (linkFileLength == 0) {
    // Place in current directory with target filename
    const char *filename = getFilename(target);
    if (filename == NULL) {
      return -1;
    }
    
    outputPath = (char*) malloc(strlen(filename) + 1);
    if (outputPath == NULL) {
      return -1;
    }
    strcpy(outputPath, filename);
  } else if (linkFile[linkFileLength - 1] == '/') {
    // Place in specified directory with target filename
    const char *filename = getFilename(target);
    if (filename == NULL) {
      return -1;
    }
    
    outputPath = (char*) malloc(strlen(linkFile) + strlen(filename) + 1);
    if (outputPath == NULL) {
      return -1;
    }
    strcpy(outputPath, linkFile);
    strcat(outputPath, filename);
  } else {
    // Use linkFile as-is
    outputPath = (char*) malloc(strlen(linkFile) + 1);
    if (outputPath == NULL) {
      return -1;
    }
    strcpy(outputPath, linkFile);
  }
  
  if (strcmp(outputPath, target) == 0) {
    // We've computed the target file as the output file.  If we continue,
    // we'll corrupt the original input.  We can't do this.  Bail.
    free(outputPath);
    return -1;
  }
  
  // Calculate total buffer size needed
  size_t pathLen = strlen(target) + 1;
  if (pathLen > 255) {
    // Path too long.  We can't store this in version 1.
    free(outputPath);
    return -1;
  }
  size_t totalSize = LINK_VERSION1_HEADER_SIZE + LINK_TYPE_LENGTH_SIZE
    + pathLen + LINK_CHECKSUM_SIZE;
  
  // Allocate buffer
  uint8_t *buffer = (uint8_t*) malloc(totalSize);
  if (!buffer) {
    free(outputPath);
    return -1;
  }
  
  // Populate buffer
  // For the header, everything is 16-bit aligned, so we don't need to use
  // memcpy, we can just direclty set the values.
  
  // Magic value.  This is at index 0, which is 64-bit aligned, so we can
  // directly set that too.
  *((uint64_t*) &buffer[LINK_MAGIC_INDEX]) = *LINK_MAGIC;
  
  // Header size.
  *((uint16_t*) &buffer[LINK_HEADER_SIZE_INDEX])
    = LINK_VERSION1_HEADER_SIZE;
  
  // Link version.
  *((uint16_t*) &buffer[LINK_VERSION_INDEX]) = 1;
  
  // Since the header was 16-bit aligned, the metadata for the first payload
  // element is too.
  // Link path value type.
  *((uint16_t*) &buffer[LINK_VERSION1_PATH_TYPE_INDEX])
    = LINK_VALUE_TYPE_PATH;
  
  // Path length, including terminating NUL byte and checksum.
  *((uint16_t*) &buffer[LINK_VERSION1_PATH_LENGTH_INDEX])
    = pathLen + LINK_CHECKSUM_SIZE;

  // Path contents.
  memcpy(&buffer[LINK_VERSION1_PATH_INDEX], target, pathLen);
  
  // Checksum.
  uint16_t checksum = 0;
  for (size_t ii = 0; ii < pathLen; ii++) {
    checksum += (uint16_t) target[ii];
  }
  memcpy(&buffer[LINK_VERSION1_PATH_INDEX + pathLen],
    &checksum, sizeof(checksum));
  
  // Write entire buffer to file
  FILE *fp = fopen(outputPath, _writeBinaryMode);
  if (!fp) {
    free(buffer);
    free(outputPath);
    return -1;
  }
  
  size_t written = fwrite(buffer, 1, totalSize, fp);
  fclose(fp);
  
  // Cleanup
  free(buffer);
  free(outputPath);
  
  return (written == totalSize) ? 0 : -1;
}

/// @var _readBinaryMode
///
/// @brief fopen() mode string used to open an existing link file.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _readBinaryMode[] KEEP_IN_FLASH = "rb";

/// @fn int getNextTarget(char *nextTarget, const char *linkFile)
///
/// @brief Get the next target from the provided link file.
///
/// @param nextTarget The buffer to fill with the next target.
/// @param linkFile The path to the link file on the filesystem.
///
/// @return Returns 0 on success, -1 on failure.
int getNextTarget(char *nextTarget, const char *linkFile) {
  if ((nextTarget == NULL) || (linkFile == NULL)) {
    return -1;
  }
  
  // Open file and get size
  FILE *fp = fopen(linkFile, _readBinaryMode);
  if (fp == NULL) {
    return -1;
  }
  
  // Get file size
  fseek(fp, 0, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  if (fileSize < LINK_VERSION1_HEADER_SIZE) {
    fclose(fp);
    return -1;
  } else if (fileSize > MAX_LINK_SIZE) {
    fclose(fp);
    return -1;
  }
  
  // Allocate buffer and read entire file
  const int bufferSize
    = MIN(fileSize, (LINK_VERSION1_PATH_INDEX + MAX_PATH_LENGTH + 1));
  uint8_t *buffer = (uint8_t*) malloc(bufferSize);
  if (buffer == NULL) {
    fclose(fp);
    errno = ENOMEM;
    return -1;
  }
  
  size_t bytesRead = fread(buffer, 1, bufferSize, fp);
  fclose(fp); fp = NULL;
  
  if (bytesRead != (size_t) fileSize) {
    free(buffer);
    return -1;
  }
  
  if (*((uint64_t*) &buffer[LINK_MAGIC_INDEX]) != *LINK_MAGIC) {
    // Not our link.
    free(buffer);
    return -1;
  }
  
  uint16_t linkHeaderSize = *((uint16_t*) &buffer[LINK_HEADER_SIZE_INDEX]);
  // We only understand version 1, so there's really no point in reading the
  // link version.
  // uint16_t linkVersion = *((uint16_t*) buffer[LINK_VERSION_INDEX]);
  
  const char *targetPath = NULL;
  uint16_t valueType = 0;
  uint16_t valueLength = 0;
  uint16_t ii = 0;
  // Search through the payload until we find the link path, which is the only
  // thing we understand in this version.  At this point, we can no longer
  // directly access elements in the buffer because we have no idea what the
  // alignment is now.
  for (ii = linkHeaderSize; (ii < bytesRead) && (targetPath == NULL);) {
    memcpy(&valueType, &buffer[ii], sizeof(valueType));
    ii += sizeof(valueType);
    memcpy(&valueLength, &buffer[ii], sizeof(valueLength));
    ii += sizeof(valueLength);
    if (valueType == LINK_VALUE_TYPE_PATH) {
      targetPath = (const char*) &buffer[ii];
    }
    
    ii += valueLength;
  }
  
  if (targetPath != NULL) {
    // The checksum is stored at the end of the value, so two bytes before our
    // current index.
    uint16_t storedChecksum = 0;
    memcpy(&storedChecksum, &buffer[ii - sizeof(uint16_t)], sizeof(uint16_t));
    
    uint16_t computedChecksum = 0;
    for (size_t jj = 0; targetPath[jj] != '\0'; jj++) {
      computedChecksum += (uint16_t) targetPath[jj];
    }
    
    if (computedChecksum != storedChecksum) {
      // Link corrupted.  No good.
      free(buffer);
      return -1;
    }
  }
  
  strcpy(nextTarget, targetPath);
  
  free(buffer);
  return 0;
}

/// @fn char* getTarget(const char *initialLink)
///
/// @brief Extract the linked file path from a link file.
///
/// @param initialLink The full path to the link file on the filesystem.
///
/// @return Returns a dynamically-allocated C string with the link target path
/// on success, NULL on failure.
char* getTarget(const char *initialLink) {
  if (initialLink == NULL) {
    return NULL;
  }
  
  char *slowPointer = (char*) malloc(MAX_PATH_LENGTH + 1);
  if (slowPointer == NULL) {
    // Nothing we can do.
    errno = ENOMEM;
    return NULL;
  }
  strcpy(slowPointer, initialLink);
  
  char *fastPointer = (char*) malloc(MAX_PATH_LENGTH + 1);
  if (fastPointer == NULL) {
    // Nothing we can do.
    free(slowPointer);
    errno = ENOMEM;
    return NULL;
  }
  strcpy(fastPointer, initialLink);
  
  char *nextTarget = (char*) malloc(MAX_PATH_LENGTH + 1);
  if (nextTarget == NULL) {
    // Nothing we can do.
    free(fastPointer);
    free(slowPointer);
    errno = ENOMEM;
    return NULL;
  }
  
  while (slowPointer != NULL) {
    // slowPointer traverses one link at a time.
    if (getNextTarget(nextTarget, slowPointer) != 0) {
      break;
    }
    strcpy(slowPointer, nextTarget);
    
    // fastPointer traverses two links at a time.
    if (getNextTarget(nextTarget, fastPointer) == 0) {
      strcpy(fastPointer, nextTarget);
      if (getNextTarget(nextTarget, fastPointer) == 0) {
        strcpy(fastPointer, nextTarget);
        if (strcmp(slowPointer, fastPointer) == 0) {
          // We're in an infinite loop.  Bail.
          free(slowPointer); slowPointer = NULL;
          errno = ELOOP;
        }
      }
    }
  }
  
  free(nextTarget);
  free(fastPointer);
  
  if (slowPointer != NULL) {
    // There's no reason to keep more memory allocated than we need here.
    // Shrink slowPointer down to just the length of the path plus the NULL
    // byte.
    void *check = realloc(slowPointer, strlen(slowPointer) + 1);
    if (check == NULL) {
      // This should never happen since we're shrinking memory, but the compiler
      // complains if we don't validate the pointer.
      free(slowPointer); slowPointer = NULL;
    }
    slowPointer = (char*) check;
  }
  
  return slowPointer;
}

/// @fn FILE *lopen(const *pathname, const char *mode)
///
/// @brief Open a file on the filesystem that may be specified by a path to the
/// file to open or to a link to the file to open.
///
/// @param pathname The path to the file or link on the filesystem.
/// @param mode The mode to open the file with that's consistent with the
///   standard C fopen call.
///
/// @return Returns a pointer to the opened file on success, NULL on failure.
FILE *lopen(const char *pathname, const char *mode) {
  if ((pathname == NULL) || (*pathname == '\0')) {
    // Nothing to do.
    return NULL;
  } else if ((mode == NULL) || (*mode == '\0')) {
    // The man page says we have to set errno to EINVAL.
    errno = EINVAL;
    return NULL;
  }
  
  char *target = getTarget(pathname);
  if ((target != NULL) && (strcmp(target, pathname) != 0)) {
    pathname = target;
  } else {
    // Free the memory early so it can be reused for the fopen.
    free(target); target = NULL;
  }
  
  FILE *file = fopen(pathname, mode);
  
  free(target);
  return file;
}

