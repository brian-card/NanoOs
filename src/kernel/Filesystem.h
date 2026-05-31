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

// Standard seek mode definitions
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
/// "FILESCMD" as a 64-bit, little-endian value.
#define FILESYSTEM_COMMAND_SIGNATURE ((uint64_t) 0x444D4353454C4946)

/// @struct FilesystemState
///
/// @brief State metadata the filesystem process uses to provide access to
/// files.
///
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
/// @param driverInit Pointer to the driver initialization function.
/// @param driverFopen Pointer to the driver function to open a file.
/// @param driverFread Pointer to the driver function to read a file.
/// @param driverFwrite Pointer to the driver function to write a file.
/// @param driverFclose Pointer to the driver function to close a file.
/// @param driverRemove Pointer to the driver function to remove a file.
/// @param driverFseek Pointer to the driver function to seek within a file.
/// @param driverGetFileBlockMetadata Pointer to the driver function to get the
///   block-level metadata of a file.
/// @param driverGetFilename Pointer to the driver function to get the name of
///   a file given its file handle.
typedef struct FilesystemState {
  void               *driverState;
  BlockDevice *blockDevice;
  uint8_t            *blockBuffer;
  uint16_t            blockSize;
  uint8_t             numOpenFiles;
  FILE               *openFiles;
  uint32_t            startLba;
  uint32_t            endLba;
  int (*driverInit)(struct FilesystemState* filesystemState);
  void* (*driverFopen)(
    void *driverState, const char *filePath, const char *mode);
  int32_t (*driverFread)(
    void *driverState, void *ptr, uint32_t length,
    void *fileHandle);
  int32_t (*driverFwrite)(
    void *driverState, void *ptr, uint32_t length,
    void *fileHandle);
  int (*driverFclose)(void *driverState, void *fileHandle);
  int (*driverRemove)(void *driverState, const char *pathname);
  int (*driverFseek)(void *driverState,
    void *fileHandle, long offset, int whence);
  int (*driverGetFileBlockMetadata)(void *ds, void *fileHandle,
    uint32_t *startBlock, uint32_t *numBlocks);
  const char* (*driverGetFilename)(void *fileHandle);
} FilesystemState;

/// @struct FilesystemIoCommandArgs
///
/// @brief Arguments needed for an I/O command in a filesystem.
///
/// @param signature A uint64_t value to designate this as a command structure
///   to the filesystem process.  This should always be the value
///   FILESYSTEM_COMMAND_SIGNATURE.
/// @param file A pointer to the FILE object returned from a call to fopen.
/// @param buffer A pointer to the memory that is either to be read from or
///   written to.
/// @param length The number of bytes to read into the buffer or write from the
///   buffer.
typedef struct FilesystemIoCommandArgs {
  uint64_t  signature;
  FILE     *file;
  void     *buffer;
  uint32_t  length;
} FilesystemIoCommandArgs;

/// @struct FilesystemSeekArgs
///
/// @brief Arguments needed for an fseek function call on a file.
///
/// @param signature A uint64_t value to designate this as a command structure
///   to the filesystem process.  This should always be the value
///   FILESYSTEM_COMMAND_SIGNATURE.
/// @param stream A pointer to the FILE object to adjust the position indicator
///   of.
/// @param offset The offset to apply to the position specified by the whence
///   parameter.
/// @param whence The position the offset is understood to be relative to.  One
///   of SEEK_SET (beginning of the file), SEEK_CUR (the current position of
///   the file) or SEEK_END (the end of the file).
typedef struct FilesystemSeekArgs {
  uint64_t  signature;
  FILE     *stream;
  long      offset;
  int       whence;
} FilesystemSeekArgs;

/// @struct FilesystemFopenArgs
///
/// @brief Function parameters and return value for an fopen call.
///
/// @param signature A uint64_t value to designate this as a command structure
///   to the filesystem process.  This should always be the value
///   FILESYSTEM_COMMAND_SIGNATURE.
/// @param pathname A string containing the full path to the file.
/// @param mode A string containing the mode to open the file with.
/// @param fd The numeric file descriptor to use for the file.
/// @param returnValue A pointer to the FILE that's opened on success, NULL
///   on failure.
typedef struct FilesystemFopenArgs {
  uint64_t    signature;
  const char *pathname;
  const char *mode;
  int         fd;
  FILE       *returnValue;
} FilesystemFopenArgs;

/// @struct FilesystemFcloseArgs
///
/// @brief Function parameters and return value for an fclose call.
///
/// @param signature A uint64_t value to designate this as a command structure
///   to the filesystem process.  This should always be the value
///   FILESYSTEM_COMMAND_SIGNATURE.
/// @param stream A pointer to the FILE to close.
/// @param returnValue The return value of the operation that will be passed
///   back to the handler.  This value will be set to the process's errno value.
typedef struct FilesystemFcloseArgs {
  uint64_t  signature;
  FILE     *stream;
  int       returnValue;
} FilesystemFcloseArgs;

/// @struct FilesystemRemoveArgs
///
/// @brief Function parameters and return value for a remove call.
///
/// @param signature A uint64_t value to designate this as a command structure
///   to the filesystem process.  This should always be the value
///   FILESYSTEM_COMMAND_SIGNATURE.
/// @param pathname The path to the file to remove.
/// @param returnValue The return value of the operation that will be passed
///   back to the handler.  This value will be set to the process's errno value.
typedef struct FilesystemRemoveArgs {
  uint64_t    signature;
  const char *pathname;
  int         returnValue;
} FilesystemRemoveArgs;

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

/// @typedef FilesystemCommandHandler
///
/// @brief Definition of a filesystem command handler function.
typedef int (*FilesystemCommandHandler)(FilesystemState*, ProcessMessage*);

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
  NUM_FILESYSTEM_COMMANDS,
  // Responses:
} FilesystemCommandResponse;

// Exported functionality
int getPartitionInfo(FilesystemState *fs);

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

/// @def rewind
///
/// @brief Function macro to implement the functionality of the standard C
/// rewind function.
///
/// @param stream A pointer to a previously-opened FILE object.
#define rewind(stream) \
  (void) fseek(stream, 0L, SEEK_SET)

/// @def ftell
///
/// @brief Function macro to get the current position of a file stream.
///
/// @param stream A pointer to a previously-opened FILE object.
#define ftell(stream) \
  (long) stream->currentPosition

int getFileBlockMetadataFromFile(FILE *stream, FileBlockMetadata *metadata);
int getFileBlockMetadataFromPath(const char *path, FileBlockMetadata *metadata);
void* runFilesystem(void *args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FILESYSTEM_H
