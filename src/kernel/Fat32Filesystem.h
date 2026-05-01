///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.28.2026
///
/// @file              Fat32Filesystem.h
///
/// @brief             Base FAT32 driver for NanoOs.
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

#ifndef FAT32_FILESYSTEM_H
#define FAT32_FILESYSTEM_H


#undef FILE

#define FILE C_FILE
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#undef FILE

#define FILE NanoOsFile

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct FilesystemState FilesystemState;

// FAT32 constants
#define FAT32_SECTOR_SIZE            512
#define FAT32_CLUSTER_SIZE_MIN       512
#define FAT32_CLUSTER_SIZE_MAX       \
  ( ((uint32_t)   32) \
  * ((uint32_t) 1024) \
  )
#define FAT32_MAX_FILENAME_LENGTH    255  // Long file name (LFN) limit
#define FAT32_SHORT_NAME_LENGTH      11   // 8.3 short name (8 + 3, no dot)
#define FAT32_DIRECTORY_ENTRY_SIZE   32
#define FAT32_MAX_OPEN_FILES         8
#define FAT32_LFN_CHARS_PER_ENTRY    13   // Characters per LFN entry

// FAT32 special cluster values
#define FAT32_CLUSTER_FREE           0x00000000
#define FAT32_CLUSTER_RESERVED_MIN   0x0FFFFFF0
#define FAT32_CLUSTER_RESERVED_MAX   0x0FFFFFF6
#define FAT32_CLUSTER_BAD            0x0FFFFFF7
#define FAT32_CLUSTER_EOC_MIN        0x0FFFFFF8  // End of chain minimum
#define FAT32_CLUSTER_EOC            0x0FFFFFFF  // End of chain marker
#define FAT32_CLUSTER_FIRST_VALID    2           // First valid data cluster

// FAT32 FAT entry mask (upper 4 bits are reserved)
#define FAT32_FAT_ENTRY_MASK         0x0FFFFFFF

// Directory entry types / markers
#define FAT32_ENTRY_FREE             0xE5  // Deleted entry marker
#define FAT32_ENTRY_END_OF_DIR       0x00  // End of directory marker
#define FAT32_ENTRY_KANJI_ESCAPE     0x05  // First byte 0xE5 encoded as 0x05
#define FAT32_ENTRY_DOT              0x2E  // '.' or '..' entry

// Long file name (LFN) entry constants
#define FAT32_LFN_ENTRY_ATTR         0x0F  // LFN attribute combination
#define FAT32_LFN_LAST_ENTRY_MASK    0x40  // OR'd with ordinal for last LFN entry
#define FAT32_LFN_ORDINAL_MASK       0x3F  // Mask to extract ordinal number

// File attributes
#define FAT32_ATTR_READ_ONLY         0x01
#define FAT32_ATTR_HIDDEN            0x02
#define FAT32_ATTR_SYSTEM            0x04
#define FAT32_ATTR_VOLUME_ID         0x08
#define FAT32_ATTR_DIRECTORY         0x10
#define FAT32_ATTR_ARCHIVE           0x20
#define FAT32_ATTR_LONG_NAME         \
  (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN \
  | FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID)

// Error codes
#define FAT32_SUCCESS                0
#define FAT32_ERROR                  -1
#define FAT32_FILE_NOT_FOUND         -2
#define FAT32_INVALID_PARAMETER      -3
#define FAT32_NO_MEMORY              -4
#define FAT32_DISK_FULL              -5
#define FAT32_TOO_MANY_OPEN_FILES    -6
#define FAT32_INVALID_FILESYSTEM     -7

/// @struct Fat32BiosParameterBlock
///
/// @brief FAT32 BIOS Parameter Block (BPB) and boot sector structure
typedef struct __attribute__((packed)) Fat32BiosParameterBlock {
  uint8_t   jumpBoot[3];           // Jump instruction (EB xx 90)
  uint8_t   oemName[8];            // OEM name
  uint16_t  bytesPerSector;        // Bytes per sector
  uint8_t   sectorsPerCluster;     // Sectors per cluster
  uint16_t  reservedSectorCount;   // Reserved sectors (before first FAT)
  uint8_t   numberOfFats;          // Number of FATs (typically 2)
  uint16_t  rootEntryCount;        // Root entry count (0 for FAT32)
  uint16_t  totalSectors16;        // Total sectors 16-bit (0 for FAT32)
  uint8_t   mediaType;             // Media type (0xF8 for hard disk)
  uint16_t  fatSize16;             // FAT size 16-bit (0 for FAT32)
  uint16_t  sectorsPerTrack;       // Sectors per track
  uint16_t  numberOfHeads;         // Number of heads
  uint32_t  hiddenSectors;         // Hidden sectors before partition
  uint32_t  totalSectors32;        // Total sectors 32-bit
  // FAT32-specific fields (offset 36)
  uint32_t  fatSize32;             // FAT size in sectors (FAT32)
  uint16_t  extFlags;              // Extended flags
  uint16_t  fileSystemVersion;     // File system version (0x0000)
  uint32_t  rootCluster;           // Root directory first cluster
  uint16_t  fsInfoSector;          // FSInfo sector number
  uint16_t  backupBootSector;      // Backup boot sector number
  uint8_t   reserved[12];          // Reserved
  uint8_t   driveNumber;           // Drive number
  uint8_t   reserved1;             // Reserved
  uint8_t   bootSignature;         // Extended boot signature (0x29)
  uint32_t  volumeSerialNumber;    // Volume serial number
  uint8_t   volumeLabel[11];       // Volume label
  uint8_t   fileSystemType[8];     // "FAT32   "
  uint8_t   bootCode[420];         // Boot code
  uint16_t  signatureWord;         // Boot signature (0xAA55)
} Fat32BiosParameterBlock;

/// @struct Fat32FsInfoSector
///
/// @brief FAT32 FSInfo sector structure
typedef struct __attribute__((packed)) Fat32FsInfoSector {
  uint32_t  leadSignature;         // Lead signature (0x41615252)
  uint8_t   reserved1[480];        // Reserved
  uint32_t  structSignature;       // Struct signature (0x61417272)
  uint32_t  freeCount;             // Free cluster count (0xFFFFFFFF if unknown)
  uint32_t  nextFree;              // Next free cluster hint
  uint8_t   reserved2[12];         // Reserved
  uint32_t  trailSignature;        // Trail signature (0xAA550000)
} Fat32FsInfoSector;

// FSInfo signature constants
#define FAT32_FSINFO_LEAD_SIG        0x41615252
#define FAT32_FSINFO_STRUCT_SIG      0x61417272
#define FAT32_FSINFO_TRAIL_SIG       0xAA550000
#define FAT32_FSINFO_FREE_UNKNOWN    0xFFFFFFFF

/// @struct Fat32DirectoryEntry
///
/// @brief FAT32 short (8.3) directory entry
typedef struct __attribute__((packed)) Fat32DirectoryEntry {
  uint8_t   name[11];              // Short name (8.3 format, space-padded)
  uint8_t   attributes;            // File attributes
  uint8_t   ntReserved;            // Reserved for Windows NT
  uint8_t   createTimeTenths;      // Create time fine resolution (10ms units)
  uint16_t  createTime;            // Create time
  uint16_t  createDate;            // Create date
  uint16_t  lastAccessDate;        // Last access date
  uint16_t  firstClusterHigh;      // High 16 bits of first cluster
  uint16_t  writeTime;             // Last write time
  uint16_t  writeDate;             // Last write date
  uint16_t  firstClusterLow;       // Low 16 bits of first cluster
  uint32_t  fileSize;              // File size in bytes
} Fat32DirectoryEntry;

/// @struct Fat32LfnEntry
///
/// @brief FAT32 long file name (LFN) directory entry
typedef struct __attribute__((packed)) Fat32LfnEntry {
  uint8_t   ordinal;               // Ordinal (sequence number)
  uint16_t  name1[5];              // Characters 1-5 (UTF-16LE)
  uint8_t   attributes;            // Attributes (always 0x0F)
  uint8_t   type;                  // Type (0 for LFN)
  uint8_t   checksum;              // Short name checksum
  uint16_t  name2[6];              // Characters 6-11 (UTF-16LE)
  uint16_t  firstClusterLow;       // First cluster (always 0)
  uint16_t  name3[2];              // Characters 12-13 (UTF-16LE)
} Fat32LfnEntry;

/// @struct Fat32FileHandle
///
/// @brief File handle for open FAT32 files
typedef struct Fat32FileHandle {
  uint32_t  firstCluster;          // First cluster of file
  uint32_t  currentCluster;        // Current cluster
  uint32_t  currentPosition;       // Current position in file
  uint32_t  fileSize;              // File size in bytes (FAT32: max 4 GiB)
  uint8_t   attributes;            // File attributes
  char      *fileName;             // File name
  uint32_t  directoryCluster;      // Directory containing this file
  uint32_t  directoryOffset;       // Offset in directory
  bool      canRead;               // Whether file is open for reading
  bool      canWrite;              // Whether file is open for writing
  bool      appendMode;            // Whether file is in append mode
} Fat32FileHandle;

/// @struct Fat32DriverState
///
/// @brief Driver state for FAT32 filesystem
typedef struct Fat32DriverState {
  FilesystemState*  filesystemState;        // Pointer to filesystem state
  uint16_t          bytesPerSector;         // Bytes per sector
  uint8_t           sectorsPerCluster;      // Sectors per cluster
  uint32_t          bytesPerCluster;        // Bytes per cluster
  uint32_t          reservedSectorCount;    // Reserved sector count
  uint32_t          fatStartSector;         // FAT start sector
  uint32_t          fatSizeInSectors;       // Size of one FAT in sectors
  uint8_t           numberOfFats;           // Number of FATs
  uint32_t          dataStartSector;        // First data sector
  uint32_t          rootDirectoryCluster;   // Root directory cluster
  uint32_t          totalDataClusters;      // Total data clusters
  uint16_t          fsInfoSector;           // FSInfo sector number
} Fat32DriverState;

// Function declarations
int fat32Initialize(FilesystemState* filesystemState);
void* fat32Fopen(
  void* driverState, const char* filePath, const char* mode);
int32_t fat32Fread(
  void* driverState, void* ptr, uint32_t length,
  void* fileHandle);
int32_t fat32Fwrite(
  void* driverState, void* ptr, uint32_t length,
  void* fileHandle);
int fat32Fclose(void* driverState, void* fileHandle);
int fat32Remove(void* driverState, const char* pathname);
int fat32Seek(void* driverState, void* fileHandle, long offset, int whence);
int fat32GetFileBlockMetadata(void *ds, void *fileHandle,
  uint32_t *startBlock, uint32_t *numBlocks);
const char *fat32GetFilename(void *fileHandle);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FAT32_FILESYSTEM_H
