///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.02.2024
///
/// @file              Filesystem.h
///
/// @brief             Common filesystem functionality for NanoOs.
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

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

// Custom includes
#include "BlockDevice.h"

#include "stddef.h"
#include "stdint.h"

typedef struct NanoOsFile FILE;
typedef struct msg_t ProcessMessage;

#ifdef __cplusplus
extern "C"
{
#endif

// Standard seek mode definitions.
// ***WARNING*** These have to match what is defined in userspace in
// usr/include/stdio.h.  If you change these for some reason, you MUST also
// update the definitions there!!!!
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/// @def MAX_PATH_LENGTH
///
/// @brief Maximum length of a full path on the filesystem.
#define MAX_PATH_LENGTH 255

/// @def FILESYSTEM_COMMAND_SIGNATURE
///
/// @brief Signature used in command structures sent to the filesystem process.
/// "\0FILESYS" as a 64-bit, little-endian value.
#define FILESYSTEM_COMMAND_SIGNATURE ((int64_t) 0x535953454C494600)

/// @typedef FilesystemCommandHandler
///
/// @brief A command handler function, indexed by FilesystemCommandResponse.
///
/// @param args A pointer to the FilesystemState the handler should operate
///   on, cast to a void*.  The state's own args member holds the
///   ProcessMessage being answered.
///
/// @return The same pointer passed in, cast back from void*.  (This lets a
/// caller confirm the handler ran to completion rather than, say, jumping
/// through an uninitialized function pointer and returning garbage; see the
/// overlay-callable convention this signature is shared with.)
typedef void* (*FilesystemCommandHandler)(void *args);

/// @typedef FilesystemDriverInit
///
/// @brief A function that initializes a filesystem.
///
/// @param fs A pointer to the FilesystemState the function should initialize.
///
/// @return Returns the FilesystemState pointer passed in.
typedef struct FilesystemState* (*FilesystemDriverInit)(
  struct FilesystemState *fs);

/// @struct FilesystemState
///
/// @brief State metadata the filesystem process uses to provide access to
/// files.
///
/// @param args A pointer to one of the command arguments structures, cast to a
///   void*.
/// @param driverState A pointer to the internal driver's state.
/// @param blockDevice A pointer to an allocated and initialized
///   BlockDevice to use for reading and writing blocks.
/// @param blockBuffer A pointer to to a dynamically-allocated block of memory
///   blockSize bytes in size.
/// @param blockSize The size of a block as it is known to the filesystem.
/// @param numOpenFiles The number of files currently open by the filesystem.
///   If this number is zero then the blockBuffer pointer may be NULL.
/// @param openFiles A pointer to the first FILE that's open.
/// @param startLba The address of the first block of the filesystem.
/// @param endLba The address of the last block of the filesystem.
/// @param driverInit Pointer to the driver's initialization function, or
///   NULL if no driver is linked directly into this build.  Populated by
///   whichever platform-specific HAL code creates the filesystem process (see
///   restartBuiltinFilesystem in HalCommon.c); Filesystem.c itself never names
///   a specific driver, so it stays buildable on every target regardless of
///   which -- if any -- is linked in.
/// @param commandHandlers Pointer to a NUM_FILESYSTEM_COMMANDS-length array
///   of command handler functions, indexed by FilesystemCommandResponse, or
///   NULL under the same condition as driverInit.  Which concrete
///   filesystem backs these -- FAT32 today, potentially something else in
///   the future -- is a build-time choice (which driver source is linked
///   in), not a runtime one; this is what lets that choice live in the HAL
///   layer instead of here.
typedef struct FilesystemState {
  void                           *args;
  void                           *driverState;
  BlockDevice                    *blockDevice;
  uint8_t                        *blockBuffer;
  uint16_t                        blockSize;
  uint8_t                         numOpenFiles;
  FILE                           *openFiles;
  uint32_t                        startLba;
  uint32_t                        endLba;
  FilesystemDriverInit            driverInit;
  const FilesystemCommandHandler *commandHandlers;
} FilesystemState;

/// @struct FilesystemIoCommandArgs
///
/// @brief Arguments needed for an I/O command in a filesystem.
///
/// @param file A pointer to the FILE object returned from a call to fopen.
/// @param buffer A pointer to the memory that is either to be read from or
///   written to.
/// @param length The number of bytes to read into the buffer or write from the
///   buffer.
typedef struct FilesystemIoCommandArgs {
  FILE     *file;
  void     *buffer;
  uint32_t  length;
} FilesystemIoCommandArgs;

/// @struct FilesystemSeekArgs
///
/// @brief Arguments needed for an fseek function call on a file.
///
/// @param stream A pointer to the FILE object to adjust the position indicator
///   of.
/// @param offset The offset to apply to the position specified by the whence
///   parameter.
/// @param whence The position the offset is understood to be relative to.  One
///   of SEEK_SET (beginning of the file), SEEK_CUR (the current position of
///   the file) or SEEK_END (the end of the file).
/// @param returnValue The return value that should be used for the calling
///   process.
/// @param errorNumber The errno value that should be set for the calling
///   process.
typedef struct FilesystemSeekArgs {
  FILE *stream;
  long  offset;
  int   whence;
  int   returnValue;
  int   errorNumber;
} FilesystemSeekArgs;

/// @struct FilesystemFopenArgs
///
/// @brief Function parameters and return value for an fopen call.
///
/// @param pathname A string containing the full path to the file.
/// @param mode A string containing the mode to open the file with.
/// @param fd The numeric file descriptor to use for the file.
/// @param returnValue A pointer to the FILE that's opened on success, NULL
///   on failure.
typedef struct FilesystemFopenArgs {
  char *pathname;
  char *mode;
  int   fd;
  FILE *returnValue;
} FilesystemFopenArgs;

/// @struct FilesystemFcloseArgs
///
/// @brief Function parameters and return value for an fclose call.
///
/// @param stream A pointer to the FILE to close.
/// @param returnValue The return value of the operation that will be passed
///   back from the handler.  This value will be set to the process's errno
///   value.
typedef struct FilesystemFcloseArgs {
  FILE     *stream;
  int       returnValue;
} FilesystemFcloseArgs;

/// @struct FilesystemRemoveArgs
///
/// @brief Function parameters and return value for a remove call.
///
/// @param pathname The path to the file to remove.
/// @param returnValue The return value of the operation that will be passed
///   back from the handler.  This value will be set to the process's errno
///   value.
typedef struct FilesystemRemoveArgs {
  char *pathname;
  int   returnValue;
} FilesystemRemoveArgs;

/// @struct FilesystemDumpOpenFilesArgs
///
/// @brief Function parameters and return value for FILESYSTEM_DUMP_OPEN_FILES.
///
/// @param returnValue The return value of the operation that will be passed
///   back from the handler.
typedef struct FilesystemDumpOpenFilesArgs {
  int      returnValue;
} FilesystemDumpOpenFilesArgs;

/// @struct GetFileBlockMetadataArgs
///
/// @brief Function arguments for the FILESYSTEM_GET_FILE_BLOCK_METADATA
/// command handler.
///
/// @param stream A pointer to a FILE the caller wants to find the metadata of.
/// @param metadata A pointer to a caller-supplied FileBlockMetadata structure
///   that is to be populated by the command.
typedef struct GetFileBlockMetadataArgs {
  FILE              *stream;
  FileBlockMetadata *metadata;
} GetFileBlockMetadataArgs;

/// @struct FeofArgs
///
/// @brief Function parameters and return value for the FILESYSTEM_END_OF_FILE
/// command handler.
///
/// @param stream A pointer to the FILE the caller wants to interrogate.
/// @param returnValue The return value of the operation that will be passed
///   back from the handler.
typedef struct FeofArgs {
  FILE *stream;
  int   returnValue;
} FeofArgs;

/// @def FAT32_FORMAT_SIGNATURE
///
/// @brief Signature identifying a Fat32FormatArgs structure.  "FAT32FMT" as
/// a 64-bit, little-endian value.
///
/// @details FILESYSTEM_FORMAT is a single, filesystem-agnostic command slot
/// (see FilesystemCommandResponse) that could in principle be answered by
/// any driver's format handler, but Fat32FormatArgs's shape -- and any other
/// driver's own equivalent -- is specific to what that filesystem type
/// needs. This build only ever links one driver in, so nothing today would
/// send a mismatched payload, but the FAT32 format handler checks this
/// field first regardless: it's the only thing standing between a future
/// build that links a different driver and that driver misreading someone
/// else's format arguments as its own.
#define FAT32_FORMAT_SIGNATURE ((int64_t) 0x544D463233544146)

/// @struct Fat32FormatArgs
///
/// @brief Function parameters and return value for the FILESYSTEM_FORMAT
/// command handler.
///
/// @note Named for FAT32, not the filesystem-agnostic "Filesystem*Args"
/// convention the other structs here follow: unlike open/read/write/seek,
/// which have universal semantics regardless of what's on disk, a format
/// operation's parameters are inherently specific to the filesystem type
/// being written.  A future second filesystem driver would define its own
/// args struct (and its own client-facing format function) shaped for
/// whatever parameters that filesystem type actually needs.
///
/// @param signature Must be FAT32_FORMAT_SIGNATURE.  Lets the FAT32 format
///   handler recognize and refuse a payload built for some other driver's
///   format command before touching any of the fields below.
/// @param volumeLabel A string containing the volume label to write, up to 11
///   characters.  May be NULL or empty for no label.
/// @param clusterSize The desired cluster size in bytes, or 0 to use a default
///   derived from the size of the partition.
/// @param returnValue The return value of the operation that will be passed
///   back from the handler.
typedef struct Fat32FormatArgs {
  int64_t  signature;
  char    *volumeLabel;
  uint32_t clusterSize;
  int      returnValue;
} Fat32FormatArgs;

/// @enum FilesystemCommandResponse
///
/// @brief Commands and responses understood by the filesystem inter-process
/// message handler.
typedef enum FilesystemCommandResponse {
  // Commands:
  FILESYSTEM_OPEN_FILE,
  FILESYSTEM_CLOSE_FILE,
  FILESYSTEM_READ_FILE,
  FILESYSTEM_WRITE_FILE,
  FILESYSTEM_REMOVE_FILE,
  FILESYSTEM_SEEK_FILE,
  FILESYSTEM_DUMP_OPEN_FILES,
  FILESYSTEM_GET_FILE_BLOCK_METADATA,
  FILESYSTEM_END_OF_FILE,
  FILESYSTEM_FORMAT,
  NUM_FILESYSTEM_COMMANDS,
  // Responses:
} FilesystemCommandResponse;

// Exported functionality
FILE* filesystemFopen(const char *pathname, const char *mode);
#ifdef fopen
#undef fopen
#endif // fopen
#define fopen filesystemFopen

int filesystemFclose(FILE *stream);
#ifdef fclose
#undef fclose
#endif // fclose
#define fclose filesystemFclose

int filesystemRemove(const char *pathname);
#ifdef remove
#undef remove
#endif // remove
#define remove filesystemRemove

int filesystemFSeek(FILE *stream, long offset, int whence);
#ifdef fseek
#undef feek
#endif // fseek
#define fseek filesystemFSeek

size_t filesystemFRead(void *ptr, size_t size, size_t nmemb, FILE *stream);
#ifdef fread
#undef fread
#endif // fread
#define fread filesystemFRead

size_t filesystemFWrite(
  const void *ptr, size_t size, size_t nmemb, FILE *stream);
#ifdef fwrite
#undef fwrite
#endif // fwrite
#define fwrite filesystemFWrite

int filesystemEndOfFile(FILE *stream);
#ifdef feof
#undef feof
#endif // feof
#define feof filesystemEndOfFile

long filesystemFtell(FILE *stream);
#ifdef ftell
#undef ftell
#endif // ftell
#define ftell filesystemFtell

/// @def rewind
///
/// @brief Function macro to implement the functionality of the standard C
/// rewind function.
///
/// @param stream A pointer to a previously-opened FILE object.
#define rewind(stream) \
  (void) fseek(stream, 0L, SEEK_SET)

int getFileBlockMetadataFromFile(FILE *stream, FileBlockMetadata *metadata);
int getFileBlockMetadataFromPath(const char *path, FileBlockMetadata *metadata);
void* runFilesystem(void *args);

/// @fn int fat32Format(const char *volumeLabel, uint32_t clusterSize)
///
/// @brief Format the root filesystem's partition as FAT32.
///
/// @note Named for FAT32, not "filesystemFormat": unlike fopen/fread/fwrite,
/// a format call's parameters are inherently specific to the filesystem type
/// being written, so there is no filesystem-agnostic name for it to share.
///
/// @param volumeLabel A string containing the volume label to write, up to 11
///   characters.  May be NULL or empty for no label.
/// @param clusterSize The desired cluster size in bytes, or 0 to use a
///   default derived from the size of the partition.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int fat32Format(const char *volumeLabel, uint32_t clusterSize);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FILESYSTEM_H
