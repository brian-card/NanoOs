///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.07.2026
///
/// @file              Fat32.h
///
/// @brief             Definitions in support of the FAT32 filesystem driver.
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

#ifndef FAT32_H
#define FAT32_H

#include "../../include/FilesystemUtils.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"


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

// NT reserved byte (Fat32DirectoryEntry.ntReserved) case bits.  Windows
// NT+, Linux, and mtools all set these on a short entry that has no
// accompanying LFN entry but whose original name was lowercase, rather than
// spending an LFN entry just to preserve case for a name that otherwise fits
// 8.3.  A legacy (DOS-era) writer leaves this byte 0, so the short name is
// rendered all-uppercase, as it's stored on disk.
#define FAT32_NT_RESERVED_LOWER_BASE 0x08  // Base name is lowercase
#define FAT32_NT_RESERVED_LOWER_EXT  0x10  // Extension is lowercase

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
#define FAT32_NOT_A_DIRECTORY        -8

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Translate a FAT32_* driver status code into the errno value the
///        calling process should see.
///
/// @details This lives here, not in the filesystem-agnostic client/handler
///          layers (Filesystem.c, usr/src/filesystems/common/*), because
///          only the driver knows what its own status codes mean.  The
///          driver-agnostic layers only ever see the resulting plain errno
///          value (see e.g. FilesystemOpendirArgs.errorNumber), never a
///          FAT32_* code.
///
/// @note The values returned are deliberately plain integer literals, not
/// the symbolic EINVAL/ENOENT/etc. names: this file is compiled together
/// (via direct #include, in all three filesystem build shapes) with code
/// that already includes the toolchain's own <errno.h> for unrelated
/// reasons, and that collides with src/user/NanoOsErrno.h's own,
/// differently-numbered versions of those same names.  ***WARNING*** These
/// values have to match src/user/NanoOsErrno.h.  If you change the numbering
/// there, you MUST update the values here too!
///
/// @param fat32Status One of the FAT32_* status codes defined above.
///
/// @return The corresponding errno value (using NanoOs's own numbering, from
/// NanoOsErrno.h), or 0 for FAT32_SUCCESS.
///
static inline int fat32ErrorToErrno(int fat32Status) {
  switch (fat32Status) {
    case FAT32_SUCCESS:             return 0;  // ENOERR
    case FAT32_FILE_NOT_FOUND:      return 8;  // ENOENT
    case FAT32_NOT_A_DIRECTORY:     return 25; // ENOTDIR
    case FAT32_INVALID_PARAMETER:   return 5;  // EINVAL
    case FAT32_NO_MEMORY:           return 3;  // ENOMEM
    case FAT32_DISK_FULL:           return 7;  // ENOSPC
    case FAT32_TOO_MANY_OPEN_FILES: return 2;  // EBUSY
    case FAT32_INVALID_FILESYSTEM:  return 6;  // EIO
    case FAT32_ERROR:               return 6;  // EIO
    default:                        return 6;  // EIO
  }
}

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

/// @struct Fat32DirHandle
///
/// @brief Handle for an open FAT32 directory stream (the concrete storage
/// behind an opaque DIR pointer).
typedef struct Fat32DirHandle {
  uint32_t       currentCluster;    // Cluster to resume scanning from
  uint32_t       offsetInCluster;   // Byte offset within currentCluster to
                                     // resume scanning from
  uint32_t       nextSequence;      // Next value to hand out as d_off
  struct dirent *entry;             // Storage for the most recently returned
                                     // entry (see driverReaddir).  Owned by
                                     // this handle: freed and reallocated on
                                     // every call, freed for good on close.
} Fat32DirHandle;

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

// Mode-string parsing flags used by fat32Fopen.
typedef struct Fat32OpenMode {
  bool canRead;
  bool canWrite;
  bool appendMode;
  bool mustExist;
  bool truncate;
} Fat32OpenMode;

// Lightweight result from a directory search.  Contains the 32-byte short
// entry plus enough bookkeeping to update it in place later.
typedef struct Fat32DirSearchResult {
  Fat32DirectoryEntry entry;        // Copy of the short directory entry.
  char    *longName;                // Heap-allocated LFN (or formatted 8.3).
                                    // The caller must free() this when done.
                                    // NULL when no name was resolved.
  uint32_t dirCluster;              // Cluster that contains this entry.
  uint32_t offsetInCluster;         // Byte offset within that cluster.
} Fat32DirSearchResult;


///////////////////////////////////////////////////////////////////////////////
///
/// @brief Convert a data-region cluster number to its first absolute LBA
///        (Logical Block Address).
///
/// @param ds  Pointer to an initialized Fat32DriverState.
/// @param cluster  A valid cluster number (>= FAT32_CLUSTER_FIRST_VALID).
///
/// @return The LBA of the first sector of the cluster.
///
static inline uint32_t fat32ClusterToLba(
    const Fat32DriverState *ds,
    uint32_t cluster
) {
  return ds->dataStartSector
    + (cluster - FAT32_CLUSTER_FIRST_VALID)
    * (uint32_t) ds->sectorsPerCluster;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Compute the inode number of a directory entry from its on-disk
///        location.
///
/// @details FAT32 has no inode table: a file's metadata (size, attributes,
///          timestamps) lives entirely in its directory entry, not at the
///          cluster that entry's data starts at. So, unlike a filesystem
///          with real inodes -- where the inode number is already the
///          address of the metadata -- the closest FAT32 equivalent is the
///          location of the directory entry itself: st_ino/d_ino computed
///          this way lets a future fstat()-style lookup go directly to the
///          entry with a single block read, no directory search required.
///
///          A sector's worth of LBA alone isn't unique: at 32 bytes per
///          entry, a single 512-byte sector holds 16 of them (128 for the
///          largest legal 4096-byte FAT32 sector), so the low 8 bits of the
///          result encode which slot within the sector this entry occupies.
///          8 bits comfortably covers every legal sector size with room to
///          spare, and the LBA -- shifted up by those same 8 bits -- still
///          fits easily within the 64 bits of ino_t.
///
///          Always compute this from the entry's own (dirCluster,
///          offsetInCluster) -- e.g. Fat32DirSearchResult's or
///          Fat32DirHandle's -- never from an LFN fragment that precedes it;
///          those aren't the entry that actually holds the metadata.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param dirCluster       The cluster containing the entry.
/// @param offsetInCluster  Byte offset of the entry within that cluster.
///
/// @return The computed inode number.
///
static inline ino_t fat32EntryLocationToIno(
    const Fat32DriverState *ds,
    uint32_t dirCluster,
    uint32_t offsetInCluster
) {
  uint32_t lba = fat32ClusterToLba(ds, dirCluster)
    + (offsetInCluster / ds->bytesPerSector);
  uint32_t slot = (offsetInCluster % ds->bytesPerSector)
    / FAT32_DIRECTORY_ENTRY_SIZE;

  return (((ino_t) lba) << 8) | ((ino_t) slot & 0xFF);
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Case-insensitive comparison of two null-terminated strings using
///        ASCII rules.
///
/// @param a  First string.
/// @param b  Second string.
///
/// @return 0 if the strings are equal (ignoring case), a positive value if
///         a > b, a negative value if a < b.
///
static inline int fat32StrcaseCmp(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    char ca = (*a >= 'a' && *a <= 'z') ? (*a - 32) : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? (*b - 32) : *b;
    if (ca != cb) {
      return (int) ca - (int) cb;
    }
    a++;
    b++;
  }

  return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Incorporate a single LFN (Long File Name) directory entry into an
///        assembly buffer.
///
/// @details Each LFN entry carries 13 UTF-16LE code units.  The ordinal field
///          encodes the one-based position of the fragment within the full
///          name.  Characters are extracted as low-byte ASCII; full Unicode
///          support is outside the scope of NanoOs.
///
/// @param lfn       Pointer to the LFN entry to process.
/// @param lfnBuffer Caller-supplied buffer of at least
///                  (FAT32_MAX_FILENAME_LENGTH + 1) bytes that accumulates
///                  the characters across successive calls.
///
static inline void fat32AssembleLfnEntry(
    const Fat32LfnEntry *lfn,
    char *lfnBuffer
) {
  uint8_t ordinal  = lfn->ordinal & FAT32_LFN_ORDINAL_MASK;
  int     baseIndex = (ordinal - 1) * FAT32_LFN_CHARS_PER_ENTRY;

  // Gather all 13 code units into a flat array for uniform processing.
  // Use byte-level offsets so that we never form a pointer to an unaligned
  // packed member on architectures with strict alignment requirements.
  uint16_t chars[FAT32_LFN_CHARS_PER_ENTRY];
  memcpy(&chars[0],  lfn->name1, 5 * sizeof(uint16_t));
  memcpy(&chars[5],  lfn->name2, 6 * sizeof(uint16_t));
  memcpy(&chars[11], lfn->name3, 2 * sizeof(uint16_t));

  for (int j = 0; j < FAT32_LFN_CHARS_PER_ENTRY; j++) {
    int pos = baseIndex + j;
    if (pos >= FAT32_MAX_FILENAME_LENGTH) {
      break;
    }
    if (chars[j] == 0x0000) {
      lfnBuffer[pos] = '\0';
      break;
    }
    if (chars[j] == 0xFFFF) {
      // Padding character after the null terminator in the last fragment.
      continue;
    }
    lfnBuffer[pos] = (char) (chars[j] & 0xFF);
  }

  // If this is the first entry we encounter in the on-disk sequence (i.e. the
  // one with the highest ordinal), make sure the buffer is terminated just
  // past the last character this fragment could contribute.
  if (lfn->ordinal & FAT32_LFN_LAST_ENTRY_MASK) {
    int maxPos = ordinal * FAT32_LFN_CHARS_PER_ENTRY;
    if (maxPos > FAT32_MAX_FILENAME_LENGTH) {
      maxPos = FAT32_MAX_FILENAME_LENGTH;
    }
    lfnBuffer[maxPos] = '\0';
  }
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Convert a raw 11-byte 8.3 directory name into a human-readable
///        null-terminated string (e.g. "README  TXT" -> "README.TXT").
///
/// @details Honors the ntReserved lowercase-display bits (see
///          FAT32_NT_RESERVED_LOWER_BASE / FAT32_NT_RESERVED_LOWER_EXT):
///          when set, the corresponding half of the name is rendered
///          lowercase instead of the raw (uppercase, DOS-style) on-disk
///          bytes.  This only ever applies to a short entry that has no
///          accompanying LFN entry -- a name with a real LFN is resolved
///          from that instead, and case-preserved as written.
///
/// @param raw        Pointer to the 11-byte short name in the directory entry.
/// @param ntReserved The directory entry's ntReserved byte.
/// @param formatted  [out] Caller-supplied buffer of at least 13 bytes.
///
static inline void fat32FormatShortName(
    const uint8_t *raw,
    uint8_t ntReserved,
    char *formatted
) {
  int  pos = 0;
  bool lowerBase = (ntReserved & FAT32_NT_RESERVED_LOWER_BASE) != 0;
  bool lowerExt  = (ntReserved & FAT32_NT_RESERVED_LOWER_EXT)  != 0;

  // Copy the base name (first 8 bytes), trimming trailing spaces.
  for (int i = 0; i < 8; i++) {
    if (raw[i] != ' ') {
      char c = (char) raw[i];
      if (lowerBase && (c >= 'A') && (c <= 'Z')) {
        c = (char) (c + ('a' - 'A'));
      }
      formatted[pos++] = c;
    }
  }

  // Append the extension if it is non-empty.
  bool hasExtension = false;
  for (int i = 8; i < 11; i++) {
    if (raw[i] != ' ') {
      hasExtension = true;
      break;
    }
  }

  if (hasExtension) {
    formatted[pos++] = '.';
    for (int i = 8; i < 11; i++) {
      if (raw[i] != ' ') {
        char c = (char) raw[i];
        if (lowerExt && (c >= 'A') && (c <= 'Z')) {
          c = (char) (c + ('a' - 'A'));
        }
        formatted[pos++] = c;
      }
    }
  }

  formatted[pos] = '\0';
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read a single FAT (File Allocation Table) entry for the given
///        cluster.
///
/// @param ds       Pointer to an initialized Fat32DriverState.
/// @param cluster  The cluster whose FAT entry is to be read.
/// @param value    [out] The masked 28-bit FAT entry value.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on a read failure.
///
/// @note This function uses the shared blockBuffer for the read.
///
static inline int fat32ReadFatEntry(
    Fat32DriverState *ds,
    uint32_t cluster,
    uint32_t *value
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  uint32_t fatByteOffset = cluster * sizeof(uint32_t);
  uint32_t fatSector = ds->fatStartSector
    + (fatByteOffset / ds->bytesPerSector);
  uint32_t offsetInSector = fatByteOffset % ds->bytesPerSector;

  int result = bd->readBlocks(
    bd->context, fatSector, 1, bd->blockSize, fs->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  uint32_t raw;
  memcpy(&raw, fs->blockBuffer + offsetInSector, sizeof(uint32_t));
  *value = raw & FAT32_FAT_ENTRY_MASK;

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Search a directory's cluster chain for an entry whose long or short
///        name matches @p name (case-insensitive).
///
/// @param ds          Pointer to an initialized Fat32DriverState.
/// @param dirCluster  The first cluster of the directory to search.
/// @param name        The name to look for.
/// @param result      [out] Populated on a successful match.
///
/// @return FAT32_SUCCESS if found, FAT32_FILE_NOT_FOUND if the directory was
///         exhausted without a match, or FAT32_ERROR / FAT32_NO_MEMORY on
///         failure.
///
/// @note Uses the shared blockBuffer for sector I/O.  The buffer contents are
///       undefined on return.
///
static inline int fat32SearchDirectory(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    const char *name,
    Fat32DirSearchResult *result
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  // The LFN assembly buffer is allocated on demand when the first fragment
  // of a long-name sequence is encountered, and sized to exactly
  // (ordinal * 13 + 1) bytes rather than the full 256-byte maximum.
  char *lfnBuffer = NULL;

  uint32_t currentCluster = dirCluster;
  int      status = FAT32_FILE_NOT_FOUND;
  bool     done = false;

  // Bound the cluster chain walk.  Terminating only on an end-of-chain
  // marker or a match trusts the on-disk FAT: if the chain has been
  // corrupted into a cycle, currentCluster keeps landing inside the valid
  // range forever and this loop -- which does a real SD block read every
  // iteration -- hangs the OS permanently while still showing SD activity.
  // Failing the lookup instead keeps a corrupt filesystem from taking the
  // system down with it.
  uint32_t clusterChainLength = 0;

  while (!done
      && (currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)
  ) {

    clusterChainLength++;
    if (clusterChainLength > 10000) {
      status = FAT32_ERROR;
      break;
    }

    uint32_t clusterLba = fat32ClusterToLba(ds, currentCluster);

    for (uint8_t sector = 0;
        (sector < ds->sectorsPerCluster) && !done;
        sector++) {

      int readResult = bd->readBlocks(
        bd->context, clusterLba + sector, 1,
        bd->blockSize, fs->blockBuffer);
      if (readResult != 0) {
        status = FAT32_ERROR;
        done = true;
        break;
      }

      uint32_t entriesPerSector =
        ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;

      for (uint32_t i = 0; (i < entriesPerSector) && !done; i++) {
        uint32_t entryByteOffset = i * FAT32_DIRECTORY_ENTRY_SIZE;
        Fat32DirectoryEntry *entry =
          (Fat32DirectoryEntry *) (fs->blockBuffer + entryByteOffset);

        // End-of-directory sentinel.
        if (entry->name[0] == FAT32_ENTRY_END_OF_DIR) {
          done = true;
          break;
        }

        // Deleted entry — discard any in-progress LFN assembly.
        if (entry->name[0] == FAT32_ENTRY_FREE) {
          free(lfnBuffer);
          lfnBuffer = NULL;
          continue;
        }

        // LFN fragment — accumulate it.
        if ((entry->attributes & FAT32_ATTR_LONG_NAME)
            == FAT32_ATTR_LONG_NAME) {
          Fat32LfnEntry *lfn = (Fat32LfnEntry *) entry;
          if (lfn->ordinal & FAT32_LFN_LAST_ENTRY_MASK) {
            // First on-disk entry of a new LFN sequence (highest ordinal).
            // Allocate a buffer sized to exactly this name's length.
            free(lfnBuffer);
            uint8_t ordinal =
              lfn->ordinal & FAT32_LFN_ORDINAL_MASK;
            uint32_t bufSize =
              (uint32_t) ordinal * FAT32_LFN_CHARS_PER_ENTRY + 1;
            lfnBuffer = (char *) malloc(bufSize);
            if (lfnBuffer == NULL) {
              status = FAT32_NO_MEMORY;
              done = true;
              break;
            }
            memset(lfnBuffer, 0, bufSize);
          }
          if (lfnBuffer != NULL) {
            fat32AssembleLfnEntry(lfn, lfnBuffer);
          }
          continue;
        }

        // ---- Short entry: check for a match ----

        bool match = false;

        // Try the assembled LFN first.
        if ((lfnBuffer != NULL) && (lfnBuffer[0] != '\0')) {
          match = (fat32StrcaseCmp(lfnBuffer, name) == 0);
        }

        // Fall back to the short name.
        if (!match) {
          char shortName[13];
          fat32FormatShortName(entry->name, entry->ntReserved, shortName);
          match = (fat32StrcaseCmp(shortName, name) == 0);
        }

        if (match) {
          memcpy(&result->entry, entry, sizeof(Fat32DirectoryEntry));

          if ((lfnBuffer != NULL) && (lfnBuffer[0] != '\0')) {
            // Transfer ownership of the right-sized lfnBuffer.
            result->longName = lfnBuffer;
            lfnBuffer = NULL;
          } else {
            // Allocate a small buffer for the formatted 8.3 name (max 13
            // characters including the dot and null terminator).
            result->longName = (char *) malloc(13);
            if (result->longName == NULL) {
              status = FAT32_NO_MEMORY;
              done = true;
              break;
            }
            fat32FormatShortName(entry->name, entry->ntReserved,
              result->longName);
          }

          result->dirCluster = currentCluster;
          result->offsetInCluster =
            (uint32_t) sector * ds->bytesPerSector + entryByteOffset;

          status = FAT32_SUCCESS;
          done = true;
        }

        // Reset LFN state between short entries.
        if (!done) {
          free(lfnBuffer);
          lfnBuffer = NULL;
        }
      }
    }

    // Follow the cluster chain (the directory sector data in blockBuffer is
    // no longer needed at this point, so the FAT read may safely reuse it).
    if (!done) {
      uint32_t nextCluster;
      if (fat32ReadFatEntry(ds, currentCluster, &nextCluster)
          != FAT32_SUCCESS) {
        status = FAT32_ERROR;
        done = true;
      } else {
        currentCluster = nextCluster;
      }
    }
  }

  free(lfnBuffer);
  return status;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Walk a path string, resolving every intermediate directory, and
///        return the cluster of the parent directory along with a pointer
///        into the original path at the final (file name) component.
///
/// @param ds              Pointer to an initialized Fat32DriverState.
/// @param filePath        The absolute or relative path to resolve.
/// @param parentCluster   [out] The first cluster of the directory that
///                        directly contains the target name.
/// @param fileNameOut     [out] Pointer into @p filePath at the start of the
///                        final component (the file name proper).
///
/// @return FAT32_SUCCESS if every intermediate directory was found,
///         FAT32_FILE_NOT_FOUND if an intermediate directory does not exist,
///         or a FAT32 error code on I/O failure.
///
static inline int fat32ResolveParentDirectory(
    Fat32DriverState *ds,
    const char *filePath,
    uint32_t *parentCluster,
    const char **fileNameOut
) {
  // Locate the last path separator.  The scan is bounded rather than
  // trusting filePath to be NUL-terminated: if whatever built it left it
  // unterminated, an unbounded scan runs off the end of the buffer with no
  // SD activity at all, which hangs the OS with nothing to show for it.
  const char *lastSlash = NULL;
  uint32_t pathScanLength = 0;
  for (const char *p = filePath; *p != '\0'; p++) {
    if (*p == '/') {
      lastSlash = p;
    }
    pathScanLength++;
    if (pathScanLength > 1024) {
      return FAT32_ERROR;
    }
  }

  // No separator, or the only separator is the leading slash — the file
  // lives directly in the root directory.
  if ((lastSlash == NULL) || (lastSlash == filePath)) {
    *parentCluster = ds->rootDirectoryCluster;
    *fileNameOut = (lastSlash == filePath) ? filePath + 1 : filePath;
    return FAT32_SUCCESS;
  }

  *fileNameOut = lastSlash + 1;

  // Traverse each component between the leading slash and lastSlash.
  uint32_t currentCluster = ds->rootDirectoryCluster;
  const char *start = filePath;
  if (*start == '/') {
    start++;
  }

  Fat32DirSearchResult searchResult;

  int result = FAT32_SUCCESS;
  while (start < lastSlash) {
    // Isolate the next component.
    const char *end = start;
    while ((end < lastSlash) && (*end != '/')) {
      end++;
    }

    size_t len = (size_t) (end - start);
    if (len > FAT32_MAX_FILENAME_LENGTH) {
      len = FAT32_MAX_FILENAME_LENGTH;
    }

    char *component = (char *) malloc(len + 1);
    if (component == NULL) {
      result = FAT32_NO_MEMORY;
      break;
    }
    memcpy(component, start, len);
    component[len] = '\0';

    searchResult.longName = NULL;
    result = fat32SearchDirectory(
      ds, currentCluster, component, &searchResult);
    free(component);

    if (result != FAT32_SUCCESS) {
      free(searchResult.longName);
      break;
    }

    if (!(searchResult.entry.attributes & FAT32_ATTR_DIRECTORY)) {
      free(searchResult.longName);
      result = FAT32_FILE_NOT_FOUND;
      break;
    }

    uint16_t clusterHigh;
    uint16_t clusterLow;
    memcpy(&clusterHigh, &searchResult.entry.firstClusterHigh,
      sizeof(uint16_t));
    memcpy(&clusterLow, &searchResult.entry.firstClusterLow,
      sizeof(uint16_t));
    currentCluster =
      ((uint32_t) clusterHigh << 16)
      | (uint32_t) clusterLow;

    // The long name is not needed during path traversal.
    free(searchResult.longName);

    start = end;
    if (*start == '/') {
      start++;
    }
  }

  *parentCluster = currentCluster;
  return result;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Resolve a path to the cluster of the directory it names.
///
/// @details Builds on fat32ResolveParentDirectory, which already walks every
///          intermediate path component: this just resolves the final
///          component too (special-casing an empty final component -- the
///          path named the root directory itself -- exactly like
///          fat32ResolveParentDirectory already does for "/") and rejects the
///          result if it isn't a directory.
///
/// @param ds           Pointer to an initialized Fat32DriverState.
/// @param path         The absolute or relative path to resolve.
/// @param dirClusterOut [out] The first cluster of the named directory.
///
/// @return FAT32_SUCCESS if the path names a directory, FAT32_FILE_NOT_FOUND
///         if any component doesn't exist, FAT32_NOT_A_DIRECTORY if the path
///         resolves to a regular file, or a FAT32 error code on I/O failure.
///
static inline int fat32ResolveDirectory(
    Fat32DriverState *ds,
    const char *path,
    uint32_t *dirClusterOut
) {
  uint32_t    parentCluster;
  const char *nameComponent = NULL;

  int result = fat32ResolveParentDirectory(
    ds, path, &parentCluster, &nameComponent);
  if (result != FAT32_SUCCESS) {
    return result;
  }

  if ((nameComponent == NULL) || (nameComponent[0] == '\0')) {
    // The path named the root directory itself.
    *dirClusterOut = parentCluster;
    return FAT32_SUCCESS;
  }

  Fat32DirSearchResult searchResult;
  searchResult.longName = NULL;
  result = fat32SearchDirectory(
    ds, parentCluster, nameComponent, &searchResult);
  if (result != FAT32_SUCCESS) {
    free(searchResult.longName);
    return result;
  }

  if (!(searchResult.entry.attributes & FAT32_ATTR_DIRECTORY)) {
    free(searchResult.longName);
    return FAT32_NOT_A_DIRECTORY;
  }

  uint16_t clusterHigh;
  uint16_t clusterLow;
  memcpy(&clusterHigh, &searchResult.entry.firstClusterHigh,
    sizeof(uint16_t));
  memcpy(&clusterLow, &searchResult.entry.firstClusterLow,
    sizeof(uint16_t));
  *dirClusterOut =
    ((uint32_t) clusterHigh << 16) | (uint32_t) clusterLow;

  free(searchResult.longName);
  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read the next entry from an open directory stream, advancing its
///        cursor past it.
///
/// @details This is fat32SearchDirectory's cluster/sector/entry walk, adapted
///          to resume from a saved (currentCluster, offsetInCluster) cursor
///          instead of starting at the beginning of the directory, and to
///          stop at the first valid entry instead of searching for a name
///          match.  Deleted entries (0xE5), LFN fragments (accumulated via
///          the same fat32AssembleLfnEntry used by fat32SearchDirectory), and
///          volume-label entries (FAT32_ATTR_VOLUME_ID) are skipped, not
///          returned.
///
/// @param ds      Pointer to an initialized Fat32DriverState.
/// @param handle  Pointer to the Fat32DirHandle to read from and advance.
///
/// @return FAT32_SUCCESS with handle->entry populated with a freshly
///         allocated struct dirent, FAT32_FILE_NOT_FOUND with handle->entry
///         set to NULL if the directory is exhausted, or FAT32_ERROR /
///         FAT32_NO_MEMORY on failure.
///
/// @note Uses the shared blockBuffer for sector I/O.  The buffer contents are
///       undefined on return.
///
static inline int fat32ReadDirectoryEntry(
    Fat32DriverState *ds,
    Fat32DirHandle *handle
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  free(handle->entry);
  handle->entry = NULL;

  char *lfnBuffer = NULL;

  uint32_t currentCluster = handle->currentCluster;
  uint32_t resumeOffset = handle->offsetInCluster;
  int      status = FAT32_FILE_NOT_FOUND;
  bool     done = false;

  while (!done
      && (currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)
  ) {

    uint32_t clusterLba = fat32ClusterToLba(ds, currentCluster);
    uint8_t  startSector = (uint8_t) (resumeOffset / ds->bytesPerSector);
    uint32_t startByteInSector = resumeOffset % ds->bytesPerSector;

    for (uint8_t sector = startSector;
        (sector < ds->sectorsPerCluster) && !done;
        sector++) {

      int readResult = bd->readBlocks(
        bd->context, clusterLba + sector, 1,
        bd->blockSize, fs->blockBuffer);
      if (readResult != 0) {
        status = FAT32_ERROR;
        done = true;
        break;
      }

      uint32_t entriesPerSector =
        ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;
      uint32_t startEntry = (sector == startSector)
        ? (startByteInSector / FAT32_DIRECTORY_ENTRY_SIZE)
        : 0;

      for (uint32_t i = startEntry; (i < entriesPerSector) && !done; i++) {
        uint32_t entryByteOffset = i * FAT32_DIRECTORY_ENTRY_SIZE;
        Fat32DirectoryEntry *entry =
          (Fat32DirectoryEntry *) (fs->blockBuffer + entryByteOffset);

        // Where the next entry starts, regardless of what this one turns out
        // to be -- the cursor always advances past whatever is consumed.
        uint32_t nextOffsetInCluster =
          (uint32_t) sector * ds->bytesPerSector + entryByteOffset
          + FAT32_DIRECTORY_ENTRY_SIZE;

        // End-of-directory sentinel.  Leave the cursor where it is (at the
        // sentinel) so a stale or repeated call keeps reporting the same
        // result instead of reading past it.
        if (entry->name[0] == FAT32_ENTRY_END_OF_DIR) {
          done = true;
          break;
        }

        // Deleted entry -- discard any in-progress LFN assembly and move on.
        if (entry->name[0] == FAT32_ENTRY_FREE) {
          free(lfnBuffer);
          lfnBuffer = NULL;
          handle->currentCluster = currentCluster;
          handle->offsetInCluster = nextOffsetInCluster;
          continue;
        }

        // LFN fragment -- accumulate it and move on.
        if ((entry->attributes & FAT32_ATTR_LONG_NAME)
            == FAT32_ATTR_LONG_NAME) {
          Fat32LfnEntry *lfn = (Fat32LfnEntry *) entry;
          if (lfn->ordinal & FAT32_LFN_LAST_ENTRY_MASK) {
            free(lfnBuffer);
            uint8_t ordinal =
              lfn->ordinal & FAT32_LFN_ORDINAL_MASK;
            uint32_t bufSize =
              (uint32_t) ordinal * FAT32_LFN_CHARS_PER_ENTRY + 1;
            lfnBuffer = (char *) malloc(bufSize);
            if (lfnBuffer == NULL) {
              status = FAT32_NO_MEMORY;
              done = true;
              break;
            }
            memset(lfnBuffer, 0, bufSize);
          }
          if (lfnBuffer != NULL) {
            fat32AssembleLfnEntry(lfn, lfnBuffer);
          }
          handle->currentCluster = currentCluster;
          handle->offsetInCluster = nextOffsetInCluster;
          continue;
        }

        // Volume-label entry -- not a real file or directory.
        if (entry->attributes & FAT32_ATTR_VOLUME_ID) {
          free(lfnBuffer);
          lfnBuffer = NULL;
          handle->currentCluster = currentCluster;
          handle->offsetInCluster = nextOffsetInCluster;
          continue;
        }

        // ---- Valid short entry: produce a dirent for it ----

        const char *resolvedName;
        char shortName[13];
        if ((lfnBuffer != NULL) && (lfnBuffer[0] != '\0')) {
          resolvedName = lfnBuffer;
        } else {
          fat32FormatShortName(entry->name, entry->ntReserved, shortName);
          resolvedName = shortName;
        }

        size_t nameLen = strlen(resolvedName);
        struct dirent *newEntry = (struct dirent *) malloc(
          offsetof(struct dirent, d_name) + nameLen + 1);
        if (newEntry == NULL) {
          status = FAT32_NO_MEMORY;
          done = true;
          break;
        }

        newEntry->d_ino = fat32EntryLocationToIno(
          ds, currentCluster, nextOffsetInCluster - FAT32_DIRECTORY_ENTRY_SIZE);
        newEntry->d_off = (off_t) handle->nextSequence++;
        newEntry->d_reclen =
          (unsigned short) (offsetof(struct dirent, d_name) + nameLen + 1);
        newEntry->d_type =
          (entry->attributes & FAT32_ATTR_DIRECTORY) ? DT_DIR : DT_REG;
        memcpy(newEntry->d_name, resolvedName, nameLen + 1);

        handle->entry = newEntry;
        handle->currentCluster = currentCluster;
        handle->offsetInCluster = nextOffsetInCluster;

        status = FAT32_SUCCESS;
        done = true;
      }
    }

    // Follow the cluster chain if this cluster was exhausted without
    // producing a result (the directory sector data in blockBuffer is no
    // longer needed at this point, so the FAT read may safely reuse it).
    if (!done) {
      uint32_t nextCluster;
      if (fat32ReadFatEntry(ds, currentCluster, &nextCluster)
          != FAT32_SUCCESS) {
        status = FAT32_ERROR;
        done = true;
      } else {
        currentCluster = nextCluster;
        resumeOffset = 0;
        handle->currentCluster = currentCluster;
        handle->offsetInCluster = 0;
      }
    }
  }

  free(lfnBuffer);
  return status;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Write a single FAT entry.
///
/// @details Performs a read-modify-write cycle so that the reserved upper four
///          bits of the 32-bit FAT slot are preserved.
///
/// @param ds       Pointer to an initialized Fat32DriverState.
/// @param cluster  The cluster whose FAT entry is to be written.
/// @param value    The 28-bit value to store (upper four bits are ignored).
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on an I/O failure.
///
static inline int fat32WriteFatEntry(
    Fat32DriverState *ds,
    uint32_t cluster,
    uint32_t value
) {
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;

  uint32_t fatByteOffset = cluster * sizeof(uint32_t);
  uint32_t fatSector = ds->fatStartSector
    + (fatByteOffset / ds->bytesPerSector);
  uint32_t offsetInSector = fatByteOffset % ds->bytesPerSector;

  // Read the sector containing the target FAT entry.
  int result = bd->readBlocks(
    bd->context, fatSector, 1, bd->blockSize, fs->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  // Preserve the upper four reserved bits.
  uint32_t existing;
  memcpy(&existing, fs->blockBuffer + offsetInSector, sizeof(uint32_t));
  uint32_t updated = (existing & ~FAT32_FAT_ENTRY_MASK)
    | (value & FAT32_FAT_ENTRY_MASK);
  memcpy(fs->blockBuffer + offsetInSector, &updated, sizeof(uint32_t));

  // Write the modified sector back.
  result = bd->writeBlocks(
    bd->context, fatSector, 1, bd->blockSize, fs->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  // If there is a second FAT, mirror the write.
  if (ds->numberOfFats > 1) {
    uint32_t mirrorSector = fatSector + ds->fatSizeInSectors;
    // Re-read the block buffer (the write call may not guarantee it is still
    // valid on all block devices).
    result = bd->readBlocks(
      bd->context, fatSector, 1, bd->blockSize, fs->blockBuffer);
    if (result != 0) {
      return FAT32_ERROR;
    }
    result = bd->writeBlocks(
      bd->context, mirrorSector, 1, bd->blockSize, fs->blockBuffer);
    if (result != 0) {
      return FAT32_ERROR;
    }
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Walk a cluster chain and mark every cluster in it as free.
///
/// @param ds            Pointer to an initialized Fat32DriverState.
/// @param firstCluster  The first cluster of the chain to free.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on an I/O failure.
///
static inline int fat32FreeClusterChain(
    Fat32DriverState *ds,
    uint32_t firstCluster
) {
  uint32_t current = firstCluster;

  while ((current >= FAT32_CLUSTER_FIRST_VALID)
      && (current < FAT32_CLUSTER_EOC_MIN)
  ) {
    uint32_t next;
    int result = fat32ReadFatEntry(ds, current, &next);
    if (result != FAT32_SUCCESS) {
      return result;
    }

    result = fat32WriteFatEntry(ds, current, FAT32_CLUSTER_FREE);
    if (result != FAT32_SUCCESS) {
      return result;
    }

    current = next;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read-modify-write a single directory entry at a known position.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param dirCluster       The cluster containing the entry.
/// @param offsetInCluster  Byte offset of the 32-byte entry within that
///                         cluster.
/// @param entry            The 32-byte entry to write.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on I/O failure.
///
static inline int fat32WriteDirectoryEntry(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    uint32_t offsetInCluster,
    const Fat32DirectoryEntry *entry
) {
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;

  uint32_t sectorIndex   = offsetInCluster / ds->bytesPerSector;
  uint32_t offsetInSector = offsetInCluster % ds->bytesPerSector;
  uint32_t lba = fat32ClusterToLba(ds, dirCluster) + sectorIndex;

  int result = bd->readBlocks(
    bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  memcpy(fs->blockBuffer + offsetInSector, entry,
    sizeof(Fat32DirectoryEntry));

  result = bd->writeBlocks(
    bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Truncate an existing file to zero length by freeing its cluster
///        chain and clearing the directory entry's first-cluster and size
///        fields.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param searchResult     The directory search result describing the file to
///                         truncate.  The entry within this structure is
///                         updated in place.
///
/// @return FAT32_SUCCESS on success, or a FAT32 error code on failure.
///
static inline int fat32TruncateFile(
    Fat32DriverState *ds,
    Fat32DirSearchResult *searchResult
) {
  uint16_t clusterHigh;
  uint16_t clusterLow;
  memcpy(&clusterHigh, &searchResult->entry.firstClusterHigh,
    sizeof(uint16_t));
  memcpy(&clusterLow, &searchResult->entry.firstClusterLow,
    sizeof(uint16_t));
  uint32_t firstCluster =
    ((uint32_t) clusterHigh << 16)
    | (uint32_t) clusterLow;

  if (firstCluster >= FAT32_CLUSTER_FIRST_VALID) {
    int result = fat32FreeClusterChain(ds, firstCluster);
    if (result != FAT32_SUCCESS) {
      return result;
    }
  }

  clusterHigh = 0;
  clusterLow  = 0;
  uint32_t fileSize = 0;
  memcpy(&searchResult->entry.firstClusterHigh, &clusterHigh,
    sizeof(uint16_t));
  memcpy(&searchResult->entry.firstClusterLow, &clusterLow,
    sizeof(uint16_t));
  memcpy(&searchResult->entry.fileSize, &fileSize,
    sizeof(uint32_t));

  return fat32WriteDirectoryEntry(ds,
    searchResult->dirCluster,
    searchResult->offsetInCluster,
    &searchResult->entry);
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Generate an 8.3 short name from an arbitrary long name.
///
/// @details This is a simplified generator that upper-cases the name, strips
///          illegal characters, separates the base from the last dot-delimited
///          extension, and truncates to 8+3.  If the name had to be lossy-
///          compressed a numeric tail ("~1") is appended.  No collision
///          detection against existing directory entries is performed; callers
///          that need uniqueness should increment the tail digit.
///
/// @param longName    The null-terminated long file name.
/// @param shortName   [out] Caller-supplied buffer of at least 11 bytes that
///                    receives the space-padded 8.3 name.
///
static inline void fat32GenerateShortName(
    const char *longName,
    uint8_t *shortName
) {
  memset(shortName, ' ', FAT32_SHORT_NAME_LENGTH);

  // Locate the last dot to split base from extension.
  const char *lastDot = NULL;
  for (const char *p = longName; *p != '\0'; p++) {
    if (*p == '.') {
      lastDot = p;
    }
  }

  // A leading dot is not treated as a name/extension separator.
  if (lastDot == longName) {
    lastDot = NULL;
  }

  const char *end = (lastDot != NULL) ? lastDot : longName + strlen(longName);

  // --- Base name (up to 6 characters + "~1" if lossy) ---
  bool lossy = false;
  int  baseLen = 0;
  for (const char *p = longName; (p < end) && (baseLen < 8); p++) {
    char c = *p;
    if (c == ' ' || c == '.') {
      lossy = true;
      continue;
    }
    if ((c >= 'a') && (c <= 'z')) {
      c -= 32;
    }
    shortName[baseLen++] = (uint8_t) c;
  }

  // If the long name is fundamentally different from the 8.3 encoding we
  // need a numeric tail to signal that an LFN entry is required.
  if ((end - longName) > 8) {
    lossy = true;
  }
  if (lastDot != NULL) {
    size_t extSourceLen = strlen(lastDot + 1);
    if (extSourceLen > 3) {
      lossy = true;
    }
  }

  if (lossy) {
    // Truncate the base to 6 characters and append "~1".
    if (baseLen > 6) {
      baseLen = 6;
    }
    shortName[baseLen]     = '~';
    shortName[baseLen + 1] = '1';
  }

  // --- Extension (up to 3 characters) ---
  if (lastDot != NULL) {
    const char *extSrc = lastDot + 1;
    int extLen = 0;
    for (; (*extSrc != '\0') && (extLen < 3); extSrc++) {
      char c = *extSrc;
      if ((c >= 'a') && (c <= 'z')) {
        c -= 32;
      }
      shortName[8 + extLen] = (uint8_t) c;
      extLen++;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Compute the checksum of an 8.3 short name as defined by the FAT
///        LFN specification.
///
/// @param shortName  Pointer to the 11-byte short name (no dot, space-padded).
///
/// @return The one-byte checksum.
///
static inline uint8_t fat32ShortNameChecksum(const uint8_t *shortName) {
  uint8_t sum = 0;
  for (int i = 0; i < FAT32_SHORT_NAME_LENGTH; i++) {
    sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortName[i];
  }
  return sum;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Convert a FAT32 on-disk date/time pair into a Unix time_t.
///
/// @details FAT32 stores no time zone of its own; every writer (Windows,
///          Linux, etc.) stamps entries with its own local wall-clock time.
///          A date of 0 (never set -- lastAccessDate in particular is commonly
///          left at 0 by other writers) yields the Unix epoch rather than a
///          nonsensical date.
///
/// @param fatDate  A FAT32 packed date (bits 15-9 year-1980, 8-5 month,
///                 4-0 day).
/// @param fatTime  A FAT32 packed time (bits 15-11 hours, 10-5 minutes,
///                 4-0 seconds/2).
///
/// @return The corresponding Unix time_t, or 0 if fatDate is 0.
///
static inline time_t fat32DateTimeToUnixTime(uint16_t fatDate, uint16_t fatTime) {
  if (fatDate == 0) {
    return (time_t) 0;
  }

  int year  = ((fatDate >> 9) & 0x7F) + 1980;
  int month = (fatDate >> 5) & 0x0F;
  int day   = fatDate & 0x1F;

  int hour   = (fatTime >> 11) & 0x1F;
  int minute = (fatTime >> 5) & 0x3F;
  int second = (fatTime & 0x1F) * 2;

  // Days since the Unix epoch, via Howard Hinnant's days_from_civil
  // algorithm: exact over the proleptic Gregorian calendar with no library
  // calls (mktime is unavailable in this freestanding environment).
  int y = year - (month <= 2);
  int era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t) (y - era * 400);
  uint32_t doy = (uint32_t) ((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5
    + day - 1);
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t) era * 146097 + (int64_t) doe - 719468;

  time_t localTime = (time_t)
    (days * 86400 + hour * 3600 + minute * 60 + second);

  return localTime;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Populate a struct stat from an already-resolved FAT32 short
///        directory entry.
///
/// @details Shared by driverLstat (which finds the entry by walking a path)
///          and driverIstat (which finds it directly from an inode number),
///          so the two report identical results for the same file -- this
///          is the entirety of what either driver does once it has the
///          entry in hand.
///
///          Leaves st_uid/st_gid at the FILESYSTEM_*_UNKNOWN sentinels (see
///          NanoOsStatTypes.h): FAT32 has no concept of ownership, and it's
///          the driver-agnostic FILESYSTEM_LSTAT/FILESYSTEM_ISTAT command
///          handlers' job, not this driver's, to decide what to report in
///          its place (see filesystemFixupUnknownOwnership in
///          NanoOsStatTypes.h). Permission bits are a different matter:
///          FAT32 *does* track something permission-shaped (the read-only
///          attribute), so this driver sets real, final permission bits
///          itself -- read/execute for everyone, plus write unless
///          FAT32_ATTR_READ_ONLY is set -- rather than leaving them for the
///          command handler to guess at. The command handler never touches
///          st_mode at all, for exactly this reason: it has no way to know
///          whether a given driver tracks permissions, so it must never
///          override whatever one that does already decided.
///
/// @param ds       Pointer to an initialized Fat32DriverState.
/// @param entry    The resolved short directory entry to populate from.
/// @param ino      The entry's own inode number (see
///                 fat32EntryLocationToIno), to store in st_ino.
/// @param statbuf  [out] The struct stat to populate.  Assumed to already
///                 be zeroed.
///
static inline void fat32PopulateStat(
    const Fat32DriverState *ds,
    const Fat32DirectoryEntry *entry,
    ino_t ino,
    struct stat *statbuf
) {
  statbuf->st_ino = ino;
  statbuf->st_uid = FILESYSTEM_UID_UNKNOWN;
  statbuf->st_gid = FILESYSTEM_GID_UNKNOWN;
  statbuf->st_nlink = 1;
  statbuf->st_blksize = (blksize_t) ds->bytesPerCluster;

  bool isDirectory = (entry->attributes & FAT32_ATTR_DIRECTORY) != 0;
  mode_t permissionBits = (entry->attributes & FAT32_ATTR_READ_ONLY)
    ? (S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
    : (S_IRWXU | S_IRWXG | S_IRWXO);
  statbuf->st_mode = (isDirectory ? S_IFDIR : S_IFREG) | permissionBits;

  if (!isDirectory) {
    uint32_t fileSize;
    memcpy(&fileSize, &entry->fileSize, sizeof(uint32_t));
    statbuf->st_size = (off_t) fileSize;
    statbuf->st_blocks = (blkcnt_t) ((fileSize + 511) / 512);
  }
  // FAT32 stores (and this driver reports) a directory's own size and block
  // count as 0, just like the on-disk entry: it never tracks the sum of its
  // contents as a "file size" the way a regular file's entry does.

  uint16_t writeDate;
  uint16_t writeTime;
  uint16_t createDate;
  uint16_t createTime;
  uint16_t lastAccessDate;
  memcpy(&writeDate, &entry->writeDate, sizeof(uint16_t));
  memcpy(&writeTime, &entry->writeTime, sizeof(uint16_t));
  memcpy(&createDate, &entry->createDate, sizeof(uint16_t));
  memcpy(&createTime, &entry->createTime, sizeof(uint16_t));
  memcpy(&lastAccessDate, &entry->lastAccessDate, sizeof(uint16_t));

  statbuf->st_mtime = fat32DateTimeToUnixTime(writeDate, writeTime);
  // FAT32 has no distinct metadata-change timestamp; creation time is the
  // closest available analog for st_ctime.
  statbuf->st_ctime = fat32DateTimeToUnixTime(createDate, createTime);
  // lastAccessDate has no time-of-day component on disk.
  statbuf->st_atime = fat32DateTimeToUnixTime(lastAccessDate, 0);
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Locate a run of contiguous free directory-entry slots in a
///        directory's cluster chain.
///
/// @details Scans forward from the first sector of the first cluster, counting
///          consecutive free (0xE5) or end-of-directory (0x00) entries.  On
///          success the cluster and byte-offset of the first slot are returned.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param dirCluster       First cluster of the directory.
/// @param slotsNeeded      Number of contiguous 32-byte slots required.
/// @param foundCluster     [out] Cluster containing the first slot.
/// @param foundOffset      [out] Byte offset within that cluster of the first
///                         slot.
///
/// @return FAT32_SUCCESS on success, FAT32_DISK_FULL if the directory was
///         exhausted, or FAT32_ERROR on I/O failure.
///
static inline int fat32FindFreeDirectorySlots(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    uint32_t slotsNeeded,
    uint32_t *foundCluster,
    uint32_t *foundOffset
) {
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;

  uint32_t runStart       = 0;
  uint32_t runCluster     = dirCluster;
  uint32_t runCount       = 0;
  uint32_t currentCluster = dirCluster;

  while ((currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)
  ) {

    uint32_t clusterLba = fat32ClusterToLba(ds, currentCluster);

    for (uint8_t sector = 0; sector < ds->sectorsPerCluster; sector++) {
      int readResult = bd->readBlocks(
        bd->context, clusterLba + sector, 1,
        bd->blockSize, fs->blockBuffer);
      if (readResult != 0) {
        return FAT32_ERROR;
      }

      uint32_t entriesPerSector =
        ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;

      for (uint32_t i = 0; i < entriesPerSector; i++) {
        uint32_t byteOffset =
          (uint32_t) sector * ds->bytesPerSector
          + i * FAT32_DIRECTORY_ENTRY_SIZE;
        uint8_t marker = fs->blockBuffer[i * FAT32_DIRECTORY_ENTRY_SIZE];

        if ((marker == FAT32_ENTRY_FREE)
            || (marker == FAT32_ENTRY_END_OF_DIR)
        ) {
          if (runCount == 0) {
            runCluster = currentCluster;
            runStart   = byteOffset;
          }
          runCount++;
          if (runCount >= slotsNeeded) {
            *foundCluster = runCluster;
            *foundOffset  = runStart;
            return FAT32_SUCCESS;
          }
        } else {
          runCount = 0;
        }
      }
    }

    // Follow the cluster chain.
    uint32_t nextCluster;
    if (fat32ReadFatEntry(ds, currentCluster, &nextCluster) != FAT32_SUCCESS) {
      return FAT32_ERROR;
    }
    currentCluster = nextCluster;
  }

  return FAT32_DISK_FULL;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Create a new file (or empty entry) in a directory, writing both
///        LFN entries and the short directory entry to disk.
///
/// @param ds             Pointer to an initialized Fat32DriverState.
/// @param parentCluster  First cluster of the parent directory.
/// @param fileName       The desired long file name.
/// @param result         [out] Populated with the newly-written directory
///                       entry and its location.
///
/// @return FAT32_SUCCESS on success, or a FAT32 error code on failure.
///
static inline int fat32CreateFileEntry(
    Fat32DriverState *ds,
    uint32_t parentCluster,
    const char *fileName,
    Fat32DirSearchResult *result
) {
  // Generate the 8.3 short name and compute the LFN checksum.
  uint8_t shortName[FAT32_SHORT_NAME_LENGTH];
  fat32GenerateShortName(fileName, shortName);
  uint8_t checksum = fat32ShortNameChecksum(shortName);

  // Determine how many LFN entries are required.
  size_t nameLen = strlen(fileName);
  uint32_t lfnEntries =
    (uint32_t) ((nameLen + FAT32_LFN_CHARS_PER_ENTRY - 1)
    / FAT32_LFN_CHARS_PER_ENTRY);
  uint32_t totalSlots = lfnEntries + 1; // LFN entries + 1 short entry

  // Locate a contiguous run of free slots.
  uint32_t slotCluster;
  uint32_t slotOffset;
  int status = fat32FindFreeDirectorySlots(
    ds, parentCluster, totalSlots, &slotCluster, &slotOffset);
  if (status != FAT32_SUCCESS) {
    return status;
  }

  // --- Write the LFN entries (highest ordinal first) ---
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;

  // We need to write totalSlots consecutive 32-byte entries starting at
  // (slotCluster, slotOffset).  Iterate entry by entry, reading the sector
  // when we cross a sector boundary and writing it back after modification.

  uint32_t writeCluster = slotCluster;
  uint32_t writeOffset  = slotOffset;

  Fat32LfnEntry lfn;
  Fat32DirectoryEntry shortEntry;

  int returnValue = FAT32_SUCCESS;
  for (uint32_t slot = 0; slot < totalSlots; slot++) {
    uint32_t sectorIndex   = writeOffset / ds->bytesPerSector;
    uint32_t offsetInSector = writeOffset % ds->bytesPerSector;
    uint32_t lba = fat32ClusterToLba(ds, writeCluster) + sectorIndex;

    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      returnValue = FAT32_ERROR;
      break;
    }

    if (slot < lfnEntries) {
      // LFN entry.  Ordinal 1 is the last slot we write (closest to the
      // short entry); ordinal N is the first slot.
      uint32_t ordinal = lfnEntries - slot;
      memset(&lfn, 0xFF, sizeof(Fat32LfnEntry));

      lfn.ordinal = (uint8_t) ordinal;
      if (slot == 0) {
        lfn.ordinal |= FAT32_LFN_LAST_ENTRY_MASK;
      }
      lfn.attributes     = FAT32_LFN_ENTRY_ATTR;
      lfn.type           = 0;
      lfn.checksum       = checksum;
      {
        uint16_t zero16 = 0;
        memcpy(&lfn.firstClusterLow, &zero16, sizeof(uint16_t));
      }

      // Build all 13 UTF-16LE code units in a flat, aligned array and then
      // copy them into the three packed name fragments with memcpy so that
      // we never form a pointer to an unaligned packed member.
      int charBase = ((int) ordinal - 1) * FAT32_LFN_CHARS_PER_ENTRY;
      uint16_t chars[FAT32_LFN_CHARS_PER_ENTRY]; // 13 code units
      bool terminated = false;

      for (int j = 0; j < FAT32_LFN_CHARS_PER_ENTRY; j++) {
        int srcIndex = charBase + j;
        if (terminated) {
          chars[j] = 0xFFFF;
        } else if (srcIndex >= (int) nameLen) {
          chars[j] = 0x0000;
          terminated = true;
        } else {
          chars[j] = (uint16_t) (uint8_t) fileName[srcIndex];
        }
      }

      {
        memcpy(lfn.name1, &chars[0],  5 * sizeof(uint16_t));
        memcpy(lfn.name2, &chars[5],  6 * sizeof(uint16_t));
        memcpy(lfn.name3, &chars[11], 2 * sizeof(uint16_t));
      }

      memcpy(fs->blockBuffer + offsetInSector, &lfn, sizeof(Fat32LfnEntry));
    } else {
      // Short directory entry.
      memset(&shortEntry, 0, sizeof(Fat32DirectoryEntry));
      memcpy(shortEntry.name, shortName, FAT32_SHORT_NAME_LENGTH);
      shortEntry.attributes      = FAT32_ATTR_ARCHIVE;
      {
        uint16_t zero16 = 0;
        uint32_t zero32 = 0;
        memcpy(&shortEntry.firstClusterHigh, &zero16, sizeof(uint16_t));
        memcpy(&shortEntry.firstClusterLow, &zero16, sizeof(uint16_t));
        memcpy(&shortEntry.fileSize, &zero32, sizeof(uint32_t));
      }

      memcpy(fs->blockBuffer + offsetInSector,
        &shortEntry, sizeof(Fat32DirectoryEntry));

      // Capture the result for the caller.
      memcpy(&result->entry, &shortEntry, sizeof(Fat32DirectoryEntry));
      result->dirCluster       = writeCluster;
      result->offsetInCluster  = writeOffset;

      size_t nameBytes = strlen(fileName) + 1;
      result->longName = (char *) malloc(nameBytes);
      if (result->longName != NULL) {
        memcpy(result->longName, fileName, nameBytes);
      } else {
        return FAT32_NO_MEMORY;
      }
    }

    ioResult = bd->writeBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      returnValue = FAT32_ERROR;
      break;
    }

    // Advance to the next 32-byte slot.
    writeOffset += FAT32_DIRECTORY_ENTRY_SIZE;
    if (writeOffset >= ds->bytesPerCluster) {
      // Move to the next cluster in the directory chain.
      uint32_t nextCluster;
      if (fat32ReadFatEntry(ds, writeCluster, &nextCluster)
          != FAT32_SUCCESS
      ) {
        returnValue = FAT32_ERROR;
        break;
      }
      writeCluster = nextCluster;
      writeOffset  = 0;
    }
  }

  return returnValue;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Allocate and populate a Fat32FileHandle from a resolved directory
///        entry and parsed mode flags.
///
/// @param ds            Pointer to an initialized Fat32DriverState.  Required
///                      so that the cluster chain can be walked when the file
///                      is opened in append mode.
/// @param searchResult  The directory search result for the file.
/// @param modeFlags     The parsed open-mode flags.
///
/// @return A pointer to a heap-allocated Fat32FileHandle, or NULL if the
///         allocation failed or a cluster-chain walk failed.
///
static inline Fat32FileHandle* fat32CreateFileHandle(
    Fat32DriverState *ds,
    const Fat32DirSearchResult *searchResult,
    const Fat32OpenMode *modeFlags
) {
  Fat32FileHandle *handle =
    (Fat32FileHandle *) calloc(1, sizeof(Fat32FileHandle));
  if (handle == NULL) {
    return NULL;
  }

  uint16_t clusterHigh;
  uint16_t clusterLow;
  uint32_t fileSize;
  memcpy(&clusterHigh, &searchResult->entry.firstClusterHigh,
    sizeof(uint16_t));
  memcpy(&clusterLow, &searchResult->entry.firstClusterLow,
    sizeof(uint16_t));
  memcpy(&fileSize, &searchResult->entry.fileSize,
    sizeof(uint32_t));

  handle->firstCluster =
    ((uint32_t) clusterHigh << 16)
    | (uint32_t) clusterLow;
  handle->currentCluster   = handle->firstCluster;
  handle->fileSize         = fileSize;
  handle->attributes       = searchResult->entry.attributes;
  handle->directoryCluster = searchResult->dirCluster;
  handle->directoryOffset  = searchResult->offsetInCluster;
  handle->canRead          = modeFlags->canRead;
  handle->canWrite         = modeFlags->canWrite;
  handle->appendMode       = modeFlags->appendMode;

  if (modeFlags->appendMode && (handle->fileSize > 0)) {
    handle->currentPosition = handle->fileSize;

    // Walk the cluster chain so that currentCluster points to the cluster
    // that contains the last byte of the file.  Without this, a subsequent
    // write would target the first cluster instead of appending at the end.
    uint32_t clustersToSkip =
      (handle->fileSize - 1) / ds->bytesPerCluster;
    uint32_t cluster = handle->firstCluster;
    for (uint32_t i = 0; i < clustersToSkip; i++) {
      uint32_t next;
      if (fat32ReadFatEntry(ds, cluster, &next) != FAT32_SUCCESS) {
        free(handle);
        return NULL;
      }
      cluster = next;
    }
    handle->currentCluster = cluster;
  } else {
    handle->currentPosition = 0;
  }

  handle->fileName = (char*) malloc(strlen(searchResult->longName) + 1);
  if (handle->fileName != NULL) {
    strcpy(handle->fileName, searchResult->longName);
  } else {
    free(handle); handle = NULL;
  }

  return handle;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Allocate a single free cluster from the FAT.
///
/// @details The FAT is scanned linearly starting from cluster 2 until a free
///          entry is found.  The newly-allocated cluster is marked as
///          end-of-chain (EOC).  If @p previousCluster is a valid cluster
///          number its FAT entry is updated to point to the new cluster,
///          extending the chain.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param previousCluster  The last cluster of the chain to extend, or 0 if
///                         no chaining is desired (i.e. the first cluster of
///                         a new file).
/// @param newCluster       [out] The cluster number that was allocated.
///
/// @return FAT32_SUCCESS on success, FAT32_DISK_FULL if no free cluster
///         exists, or FAT32_ERROR on an I/O failure.
///
static inline int fat32AllocateCluster(
    Fat32DriverState *ds,
    uint32_t previousCluster,
    uint32_t *newCluster
) {
  // Linear scan — simple and correct.  The FSInfo hint could speed this up,
  // but the added complexity is not warranted for NanoOs at this stage.
  for (uint32_t candidate = FAT32_CLUSTER_FIRST_VALID;
      candidate < FAT32_CLUSTER_FIRST_VALID + ds->totalDataClusters;
      candidate++) {
    uint32_t entry;
    int result = fat32ReadFatEntry(ds, candidate, &entry);
    if (result != FAT32_SUCCESS) {
      return FAT32_ERROR;
    }

    if (entry == FAT32_CLUSTER_FREE) {
      // Mark the new cluster as end-of-chain.
      result = fat32WriteFatEntry(ds, candidate, FAT32_CLUSTER_EOC);
      if (result != FAT32_SUCCESS) {
        return FAT32_ERROR;
      }

      // Chain from the previous cluster if one was supplied.
      if (previousCluster >= FAT32_CLUSTER_FIRST_VALID) {
        result = fat32WriteFatEntry(ds, previousCluster, candidate);
        if (result != FAT32_SUCCESS) {
          // Roll back: free the cluster we just marked.
          fat32WriteFatEntry(ds, candidate, FAT32_CLUSTER_FREE);
          return FAT32_ERROR;
        }
      }

      *newCluster = candidate;
      return FAT32_SUCCESS;
    }
  }

  return FAT32_DISK_FULL;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Scan a directory's cluster chain, locate the short directory entry
///        at a known position, and mark it — along with all of its preceding
///        LFN entries — as deleted (0xE5).
///
/// @details The directory is scanned forward from its first cluster.  LFN
///          entries whose checksum matches the target short name are
///          accumulated in a run.  When the short entry at the target position
///          is reached the entire run (plus the short entry itself) is
///          invalidated with read-modify-write cycles.
///
///          A run is reset whenever a deleted entry, a short entry belonging to
///          a different file, or an LFN entry with a non-matching checksum is
///          encountered, so only entries that genuinely belong to the target
///          file are affected.
///
/// @param ds               Pointer to an initialized Fat32DriverState.
/// @param dirFirstCluster  The first cluster of the directory that contains
///                         the target entry.
/// @param target           The search result produced by fat32SearchDirectory
///                         for the file being removed.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on an I/O failure, or
///         FAT32_FILE_NOT_FOUND if the target entry was not encountered
///         (should not happen when the caller has already confirmed the file
///         exists).
///
static inline int fat32InvalidateDirectoryEntries(
    Fat32DriverState *ds,
    uint32_t dirFirstCluster,
    const Fat32DirSearchResult *target
) {
  uint8_t checksum = fat32ShortNameChecksum(target->entry.name);

  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  // A 255-character file name requires at most ceil(255 / 13) = 20 LFN
  // entries.  We track their (cluster, offsetInCluster) positions so that
  // we can mark them as deleted once the matching short entry is confirmed.
  uint32_t runClusters[FAT32_MAX_FILENAME_LENGTH / FAT32_LFN_CHARS_PER_ENTRY
    + 1];
  uint32_t runOffsets[FAT32_MAX_FILENAME_LENGTH / FAT32_LFN_CHARS_PER_ENTRY
    + 1];
  uint32_t runCount = 0;

  uint32_t currentCluster = dirFirstCluster;
  bool     done = false;

  while (!done
      && (currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)
  ) {

    uint32_t clusterLba = fat32ClusterToLba(ds, currentCluster);

    for (uint8_t sector = 0;
        (sector < ds->sectorsPerCluster) && !done;
        sector++) {

      int ioResult = bd->readBlocks(
        bd->context, clusterLba + sector, 1,
        bd->blockSize, fs->blockBuffer);
      if (ioResult != 0) {
        return FAT32_ERROR;
      }

      uint32_t entriesPerSector =
        ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;

      for (uint32_t i = 0; (i < entriesPerSector) && !done; i++) {
        uint32_t entryByteOffset = i * FAT32_DIRECTORY_ENTRY_SIZE;
        uint32_t offsetInCluster =
          (uint32_t) sector * ds->bytesPerSector + entryByteOffset;

        Fat32DirectoryEntry *entry =
          (Fat32DirectoryEntry *) (fs->blockBuffer + entryByteOffset);

        // End-of-directory sentinel — the target must have already been
        // encountered if it exists.
        if (entry->name[0] == FAT32_ENTRY_END_OF_DIR) {
          done = true;
          break;
        }

        // Deleted entry — break the current LFN run.
        if (entry->name[0] == FAT32_ENTRY_FREE) {
          runCount = 0;
          continue;
        }

        // ---- LFN fragment ----
        if ((entry->attributes & FAT32_ATTR_LONG_NAME)
            == FAT32_ATTR_LONG_NAME) {
          Fat32LfnEntry *lfn = (Fat32LfnEntry *) entry;

          // A "last" LFN entry (highest ordinal) marks the start of a new
          // on-disk sequence.
          if (lfn->ordinal & FAT32_LFN_LAST_ENTRY_MASK) {
            runCount = 0;
          }

          if (lfn->checksum == checksum) {
            if (runCount
                < (FAT32_MAX_FILENAME_LENGTH / FAT32_LFN_CHARS_PER_ENTRY + 1)
            ) {
              runClusters[runCount] = currentCluster;
              runOffsets[runCount]  = offsetInCluster;
              runCount++;
            }
          } else {
            // Checksum mismatch — these LFN entries belong to a different
            // file; discard the run.
            runCount = 0;
          }
          continue;
        }

        // ---- Short entry ----
        if ((currentCluster == target->dirCluster)
            && (offsetInCluster == target->offsetInCluster)
        ) {
          // This is the target.  Mark it as deleted in the buffer that is
          // still loaded from the most recent readBlocks.
          fs->blockBuffer[entryByteOffset] = FAT32_ENTRY_FREE;
          ioResult = bd->writeBlocks(
            bd->context, clusterLba + sector, 1,
            bd->blockSize, fs->blockBuffer);
          if (ioResult != 0) {
            return FAT32_ERROR;
          }

          // Now mark each preceding LFN entry as deleted.
          for (uint32_t li = 0; li < runCount; li++) {
            uint32_t lfnSectorIdx =
              runOffsets[li] / ds->bytesPerSector;
            uint32_t lfnOffInSec =
              runOffsets[li] % ds->bytesPerSector;
            uint32_t lfnLba =
              fat32ClusterToLba(ds, runClusters[li]) + lfnSectorIdx;

            ioResult = bd->readBlocks(
              bd->context, lfnLba, 1, bd->blockSize, fs->blockBuffer);
            if (ioResult != 0) {
              return FAT32_ERROR;
            }

            fs->blockBuffer[lfnOffInSec] = FAT32_ENTRY_FREE;

            ioResult = bd->writeBlocks(
              bd->context, lfnLba, 1, bd->blockSize, fs->blockBuffer);
            if (ioResult != 0) {
              return FAT32_ERROR;
            }
          }

          return FAT32_SUCCESS;
        }

        // A short entry for a different file — reset the LFN run.
        runCount = 0;
      }
    }

    // Follow the directory's cluster chain.
    if (!done) {
      uint32_t nextCluster;
      if (fat32ReadFatEntry(ds, currentCluster, &nextCluster)
          != FAT32_SUCCESS) {
        return FAT32_ERROR;
      }
      currentCluster = nextCluster;
    }
  }

  return FAT32_FILE_NOT_FOUND;
}

#ifdef __cplusplus
}
#endif

#endif // FAT32_H

