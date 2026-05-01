////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file Fat32Filesystem.c
///
/// @brief Low-level implementation of the FAT32 driver.

// Standard C includes
#include "string.h"

// NanoOs includes
#include "BlockStorage.h"
#include "Fat32Filesystem.h"
#include "Filesystem.h"
#include "MemoryManager.h"


///////////////////////////////////////////////////////////////////////////////
//
// Helper declarations
//
///////////////////////////////////////////////////////////////////////////////

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
  char  longName[FAT32_MAX_FILENAME_LENGTH + 1]; // Assembled LFN or "".
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
uint32_t fat32ClusterToLba(
    const Fat32DriverState *ds,
    uint32_t cluster
) {
  return ds->dataStartSector
    + (cluster - FAT32_CLUSTER_FIRST_VALID)
    * (uint32_t) ds->sectorsPerCluster;
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
int fat32ReadFatEntry(
    Fat32DriverState *ds,
    uint32_t cluster,
    uint32_t *value
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

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
int fat32WriteFatEntry(
    Fat32DriverState *ds,
    uint32_t cluster,
    uint32_t value
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

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
int fat32FreeClusterChain(
    Fat32DriverState *ds,
    uint32_t firstCluster
) {
  uint32_t current = firstCluster;

  while ((current >= FAT32_CLUSTER_FIRST_VALID)
      && (current < FAT32_CLUSTER_EOC_MIN)) {
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
/// @brief Case-insensitive comparison of two null-terminated strings using
///        ASCII rules.
///
/// @param a  First string.
/// @param b  Second string.
///
/// @return 0 if the strings are equal (ignoring case), a positive value if
///         a > b, a negative value if a < b.
///
int fat32StrcaseCmp(const char *a, const char *b) {
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
/// @brief Convert a raw 11-byte 8.3 directory name into a human-readable
///        null-terminated string (e.g. "README  TXT" -> "README.TXT").
///
/// @param raw        Pointer to the 11-byte short name in the directory entry.
/// @param formatted  [out] Caller-supplied buffer of at least 13 bytes.
///
void fat32FormatShortName(
    const uint8_t *raw,
    char *formatted
) {
  int pos = 0;

  // Copy the base name (first 8 bytes), trimming trailing spaces.
  for (int i = 0; i < 8; i++) {
    if (raw[i] != ' ') {
      formatted[pos++] = (char) raw[i];
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
        formatted[pos++] = (char) raw[i];
      }
    }
  }

  formatted[pos] = '\0';
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
void fat32AssembleLfnEntry(
    const Fat32LfnEntry *lfn,
    char *lfnBuffer
) {
  uint8_t ordinal  = lfn->ordinal & FAT32_LFN_ORDINAL_MASK;
  int     baseIndex = (ordinal - 1) * FAT32_LFN_CHARS_PER_ENTRY;

  // Gather all 13 code units into a flat array for uniform processing.
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
/// @brief Compute the checksum of an 8.3 short name as defined by the FAT
///        LFN specification.
///
/// @param shortName  Pointer to the 11-byte short name (no dot, space-padded).
///
/// @return The one-byte checksum.
///
uint8_t fat32ShortNameChecksum(const uint8_t *shortName) {
  uint8_t sum = 0;
  for (int i = 0; i < FAT32_SHORT_NAME_LENGTH; i++) {
    sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortName[i];
  }
  return sum;
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
int fat32SearchDirectory(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    const char *name,
    Fat32DirSearchResult *result
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

  char *lfnBuffer = (char *) malloc(FAT32_MAX_FILENAME_LENGTH + 1);
  if (lfnBuffer == NULL) {
    return FAT32_NO_MEMORY;
  }
  lfnBuffer[0] = '\0';

  uint32_t currentCluster = dirCluster;
  int      status = FAT32_FILE_NOT_FOUND;
  bool     done = false;

  while (!done
      && (currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)) {

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

        // Deleted entry — reset LFN state and move on.
        if (entry->name[0] == FAT32_ENTRY_FREE) {
          lfnBuffer[0] = '\0';
          continue;
        }

        // LFN fragment — accumulate it.
        if ((entry->attributes & FAT32_ATTR_LONG_NAME)
            == FAT32_ATTR_LONG_NAME) {
          Fat32LfnEntry *lfn = (Fat32LfnEntry *) entry;
          if (lfn->ordinal & FAT32_LFN_LAST_ENTRY_MASK) {
            memset(lfnBuffer, 0, FAT32_MAX_FILENAME_LENGTH + 1);
          }
          fat32AssembleLfnEntry(lfn, lfnBuffer);
          continue;
        }

        // ---- Short entry: check for a match ----

        bool match = false;

        // Try the assembled LFN first.
        if (lfnBuffer[0] != '\0') {
          match = (fat32StrcaseCmp(lfnBuffer, name) == 0);
        }

        // Fall back to the short name.
        if (!match) {
          char shortName[13];
          fat32FormatShortName(entry->name, shortName);
          match = (fat32StrcaseCmp(shortName, name) == 0);
        }

        if (match) {
          memcpy(&result->entry, entry, sizeof(Fat32DirectoryEntry));

          if (lfnBuffer[0] != '\0') {
            memcpy(result->longName, lfnBuffer,
              FAT32_MAX_FILENAME_LENGTH + 1);
          } else {
            fat32FormatShortName(entry->name, result->longName);
          }

          result->dirCluster = currentCluster;
          result->offsetInCluster =
            (uint32_t) sector * ds->bytesPerSector + entryByteOffset;

          status = FAT32_SUCCESS;
          done = true;
        }

        lfnBuffer[0] = '\0';
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
/// @brief Parse a C standard library mode string ("r", "w", "a" and their "+"
///        variants) into a set of boolean flags.
///
/// @param mode   The null-terminated mode string.
/// @param flags  [out] Populated with the parsed flags.
///
/// @return FAT32_SUCCESS on a recognised mode, FAT32_INVALID_PARAMETER
///         otherwise.
///
int fat32ParseMode(const char *mode, Fat32OpenMode *flags) {
  memset(flags, 0, sizeof(Fat32OpenMode));

  if ((mode == NULL) || (mode[0] == '\0')) {
    return FAT32_INVALID_PARAMETER;
  }

  switch (mode[0]) {
    case 'r':
      flags->canRead   = true;
      flags->mustExist = true;
      if (mode[1] == '+') {
        flags->canWrite = true;
      }
      break;

    case 'w':
      flags->canWrite  = true;
      flags->truncate  = true;
      if (mode[1] == '+') {
        flags->canRead = true;
      }
      break;

    case 'a':
      flags->canWrite    = true;
      flags->appendMode  = true;
      if (mode[1] == '+') {
        flags->canRead = true;
      }
      break;

    default:
      return FAT32_INVALID_PARAMETER;
  }

  return FAT32_SUCCESS;
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
int fat32ResolveParentDirectory(
    Fat32DriverState *ds,
    const char *filePath,
    uint32_t *parentCluster,
    const char **fileNameOut
) {
  // Locate the last path separator.
  const char *lastSlash = NULL;
  for (const char *p = filePath; *p != '\0'; p++) {
    if (*p == '/') {
      lastSlash = p;
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

  char *component = (char *) malloc(FAT32_MAX_FILENAME_LENGTH + 1);
  if (component == NULL) {
    return FAT32_NO_MEMORY;
  }

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
    memcpy(component, start, len);
    component[len] = '\0';

    Fat32DirSearchResult searchResult;
    result = fat32SearchDirectory(
      ds, currentCluster, component, &searchResult);
    if (result != FAT32_SUCCESS) {
      break;
    }

    if (!(searchResult.entry.attributes & FAT32_ATTR_DIRECTORY)) {
      result = FAT32_FILE_NOT_FOUND;
      break;
    }

    currentCluster =
      ((uint32_t) searchResult.entry.firstClusterHigh << 16)
      | (uint32_t) searchResult.entry.firstClusterLow;

    start = end;
    if (*start == '/') {
      start++;
    }
  }

  free(component);
  *parentCluster = currentCluster;
  return result;
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
int fat32WriteDirectoryEntry(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    uint32_t offsetInCluster,
    const Fat32DirectoryEntry *entry
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

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
int fat32TruncateFile(
    Fat32DriverState *ds,
    Fat32DirSearchResult *searchResult
) {
  uint32_t firstCluster =
    ((uint32_t) searchResult->entry.firstClusterHigh << 16)
    | (uint32_t) searchResult->entry.firstClusterLow;

  if (firstCluster >= FAT32_CLUSTER_FIRST_VALID) {
    int result = fat32FreeClusterChain(ds, firstCluster);
    if (result != FAT32_SUCCESS) {
      return result;
    }
  }

  searchResult->entry.firstClusterHigh = 0;
  searchResult->entry.firstClusterLow  = 0;
  searchResult->entry.fileSize         = 0;

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
void fat32GenerateShortName(
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
int fat32FindFreeDirectorySlots(
    Fat32DriverState *ds,
    uint32_t dirCluster,
    uint32_t slotsNeeded,
    uint32_t *foundCluster,
    uint32_t *foundOffset
) {
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

  uint32_t runStart       = 0;
  uint32_t runCluster     = dirCluster;
  uint32_t runCount       = 0;
  uint32_t currentCluster = dirCluster;

  while ((currentCluster >= FAT32_CLUSTER_FIRST_VALID)
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)) {

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
            || (marker == FAT32_ENTRY_END_OF_DIR)) {
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
int fat32CreateFileEntry(
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
  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

  // We need to write totalSlots consecutive 32-byte entries starting at
  // (slotCluster, slotOffset).  Iterate entry by entry, reading the sector
  // when we cross a sector boundary and writing it back after modification.

  uint32_t writeCluster = slotCluster;
  uint32_t writeOffset  = slotOffset;

  for (uint32_t slot = 0; slot < totalSlots; slot++) {
    uint32_t sectorIndex   = writeOffset / ds->bytesPerSector;
    uint32_t offsetInSector = writeOffset % ds->bytesPerSector;
    uint32_t lba = fat32ClusterToLba(ds, writeCluster) + sectorIndex;

    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return FAT32_ERROR;
    }

    if (slot < lfnEntries) {
      // LFN entry.  Ordinal 1 is the last slot we write (closest to the
      // short entry); ordinal N is the first slot.
      uint32_t ordinal = lfnEntries - slot;
      Fat32LfnEntry lfn;
      memset(&lfn, 0xFF, sizeof(Fat32LfnEntry));

      lfn.ordinal = (uint8_t) ordinal;
      if (slot == 0) {
        lfn.ordinal |= FAT32_LFN_LAST_ENTRY_MASK;
      }
      lfn.attributes     = FAT32_LFN_ENTRY_ATTR;
      lfn.type           = 0;
      lfn.checksum       = checksum;
      lfn.firstClusterLow = 0;

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

      memcpy(lfn.name1, &chars[0],  5 * sizeof(uint16_t));
      memcpy(lfn.name2, &chars[5],  6 * sizeof(uint16_t));
      memcpy(lfn.name3, &chars[11], 2 * sizeof(uint16_t));

      memcpy(fs->blockBuffer + offsetInSector, &lfn, sizeof(Fat32LfnEntry));
    } else {
      // Short directory entry.
      Fat32DirectoryEntry shortEntry;
      memset(&shortEntry, 0, sizeof(Fat32DirectoryEntry));
      memcpy(shortEntry.name, shortName, FAT32_SHORT_NAME_LENGTH);
      shortEntry.attributes      = FAT32_ATTR_ARCHIVE;
      shortEntry.firstClusterHigh = 0;
      shortEntry.firstClusterLow  = 0;
      shortEntry.fileSize         = 0;

      memcpy(fs->blockBuffer + offsetInSector,
        &shortEntry, sizeof(Fat32DirectoryEntry));

      // Capture the result for the caller.
      memcpy(&result->entry, &shortEntry, sizeof(Fat32DirectoryEntry));
      result->dirCluster       = writeCluster;
      result->offsetInCluster  = writeOffset;
      strncpy(result->longName, fileName, FAT32_MAX_FILENAME_LENGTH);
      result->longName[FAT32_MAX_FILENAME_LENGTH] = '\0';
    }

    ioResult = bd->writeBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return FAT32_ERROR;
    }

    // Advance to the next 32-byte slot.
    writeOffset += FAT32_DIRECTORY_ENTRY_SIZE;
    if (writeOffset >= ds->bytesPerCluster) {
      // Move to the next cluster in the directory chain.
      uint32_t nextCluster;
      if (fat32ReadFatEntry(ds, writeCluster, &nextCluster)
          != FAT32_SUCCESS) {
        return FAT32_ERROR;
      }
      writeCluster = nextCluster;
      writeOffset  = 0;
    }
  }

  return FAT32_SUCCESS;
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
Fat32FileHandle* fat32CreateFileHandle(
    Fat32DriverState *ds,
    const Fat32DirSearchResult *searchResult,
    const Fat32OpenMode *modeFlags
) {
  Fat32FileHandle *handle =
    (Fat32FileHandle *) calloc(1, sizeof(Fat32FileHandle));
  if (handle == NULL) {
    return NULL;
  }

  handle->firstCluster =
    ((uint32_t) searchResult->entry.firstClusterHigh << 16)
    | (uint32_t) searchResult->entry.firstClusterLow;
  handle->currentCluster   = handle->firstCluster;
  handle->fileSize         = searchResult->entry.fileSize;
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
int fat32AllocateCluster(
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
int fat32InvalidateDirectoryEntries(
    Fat32DriverState *ds,
    uint32_t dirFirstCluster,
    const Fat32DirSearchResult *target
) {
  uint8_t checksum = fat32ShortNameChecksum(target->entry.name);

  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

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
      && (currentCluster < FAT32_CLUSTER_EOC_MIN)) {

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
                < (FAT32_MAX_FILENAME_LENGTH / FAT32_LFN_CHARS_PER_ENTRY
                  + 1)) {
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
            && (offsetInCluster == target->offsetInCluster)) {
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


///////////////////////////////////////////////////////////////////////////////
//
// Public API
//
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read and validate the FAT32 BPB (BIOS Parameter Block), allocate
///        a Fat32DriverState, and install it in the FilesystemState.
///
/// @details The caller must have already invoked getPartitionInfo (so that
///          startLba and endLba are set) and allocated blockBuffer to at least
///          blockSize bytes.
///
/// @param filesystemState  Pointer to a FilesystemState whose blockDevice,
///                         startLba, endLba, blockBuffer, and blockSize fields
///                         are already valid.
///
/// @return FAT32_SUCCESS on success, or one of the FAT32 error codes on
///         failure.
///
int fat32Initialize(FilesystemState *filesystemState) {
  BlockStorageDevice *blockDevice = filesystemState->blockDevice;

  // Read the partition's boot sector into the pre-allocated block buffer.
  int returnValue = blockDevice->readBlocks(
    blockDevice->context,
    filesystemState->startLba,
    1,
    blockDevice->blockSize,
    filesystemState->blockBuffer);
  if (returnValue != 0) {
    return FAT32_ERROR;
  }

  Fat32BiosParameterBlock *bpb =
    (Fat32BiosParameterBlock *) filesystemState->blockBuffer;

  // --- Validate the BPB ---
  if (bpb->signatureWord != 0xAA55) {
    return FAT32_INVALID_FILESYSTEM;
  }
  if ((bpb->fatSize16 != 0)
      || (bpb->fatSize32 == 0)
      || (bpb->rootEntryCount != 0)) {
    return FAT32_INVALID_FILESYSTEM;
  }
  if ((bpb->bytesPerSector < FAT32_SECTOR_SIZE)
      || (bpb->sectorsPerCluster == 0)
      || ((bpb->sectorsPerCluster & (bpb->sectorsPerCluster - 1)) != 0)) {
    return FAT32_INVALID_FILESYSTEM;
  }

  // --- Allocate and populate the driver state ---
  Fat32DriverState *driverState =
    (Fat32DriverState *) calloc(1, sizeof(Fat32DriverState));
  if (driverState == NULL) {
    return FAT32_NO_MEMORY;
  }

  driverState->filesystemState     = filesystemState;
  driverState->bytesPerSector      = bpb->bytesPerSector;
  driverState->sectorsPerCluster   = bpb->sectorsPerCluster;
  driverState->bytesPerCluster     =
    (uint32_t) bpb->sectorsPerCluster * (uint32_t) bpb->bytesPerSector;
  driverState->reservedSectorCount = bpb->reservedSectorCount;
  driverState->numberOfFats        = bpb->numberOfFats;
  driverState->fatSizeInSectors    = bpb->fatSize32;
  driverState->rootDirectoryCluster = bpb->rootCluster;
  driverState->fsInfoSector        = bpb->fsInfoSector;

  driverState->fatStartSector =
    filesystemState->startLba + bpb->reservedSectorCount;

  driverState->dataStartSector = driverState->fatStartSector
    + ((uint32_t) bpb->numberOfFats * bpb->fatSize32);

  uint32_t totalSectors = (bpb->totalSectors32 != 0)
    ? bpb->totalSectors32
    : (uint32_t) bpb->totalSectors16;
  uint32_t dataSectors = totalSectors
    - (driverState->dataStartSector - filesystemState->startLba);
  driverState->totalDataClusters =
    dataSectors / bpb->sectorsPerCluster;

  filesystemState->driverState = driverState;

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Open a file on a FAT32 filesystem.
///
/// @details Resolves the full path by walking intermediate directories,
///          searches the target directory for the file name, and applies the
///          semantics of the C standard library mode string:
///
///          "r"  — open for reading; file must exist.
///          "r+" — open for reading and writing; file must exist.
///          "w"  — open for writing; create or truncate.
///          "w+" — open for reading and writing; create or truncate.
///          "a"  — open for appending; create if absent.
///          "a+" — open for reading and appending; create if absent.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param filePath     The null-terminated path to the file.
/// @param mode         The null-terminated C fopen mode string.
///
/// @return A pointer to a heap-allocated Fat32FileHandle on success, or NULL
///         on failure.
///
void* fat32Fopen(
    void *driverState,
    const char *filePath,
    const char *mode
) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;
  if ((ds == NULL) || (filePath == NULL) || (mode == NULL)) {
    return NULL;
  }

  // ---- Parse the mode string ----
  Fat32OpenMode modeFlags;
  if (fat32ParseMode(mode, &modeFlags) != FAT32_SUCCESS) {
    return NULL;
  }

  // ---- Resolve the parent directory ----
  uint32_t    parentCluster;
  const char *fileName = NULL;

  if (fat32ResolveParentDirectory(ds, filePath, &parentCluster, &fileName)
      != FAT32_SUCCESS) {
    return NULL;
  }

  if ((fileName == NULL) || (fileName[0] == '\0')) {
    return NULL;
  }

  // ---- Search for the file in the parent directory ----
  Fat32DirSearchResult *searchResult =
    (Fat32DirSearchResult *) malloc(sizeof(Fat32DirSearchResult));
  if (searchResult == NULL) {
    return NULL;
  }

  int searchStatus = fat32SearchDirectory(
    ds, parentCluster, fileName, searchResult);

  if (searchStatus == FAT32_SUCCESS) {
    // File exists.

    // Reject attempts to open a directory as a regular file.
    if (searchResult->entry.attributes & FAT32_ATTR_DIRECTORY) {
      free(searchResult);
      return NULL;
    }

    // "w" / "w+" — truncate the existing file to zero length.
    if (modeFlags.truncate) {
      if (fat32TruncateFile(ds, searchResult) != FAT32_SUCCESS) {
        free(searchResult);
        return NULL;
      }
    }
  } else if (searchStatus == FAT32_FILE_NOT_FOUND) {
    // File does not exist.

    if (modeFlags.mustExist) {
      // "r" / "r+" — the file must already be present.
      free(searchResult);
      return NULL;
    }

    // "w", "w+", "a", "a+" — create a new, empty file.
    if (fat32CreateFileEntry(ds, parentCluster, fileName, searchResult)
        != FAT32_SUCCESS) {
      free(searchResult);
      return NULL;
    }
  } else {
    // An I/O or allocation error occurred during the search.
    free(searchResult);
    return NULL;
  }

  // ---- Build the file handle ----
  Fat32FileHandle *handle = fat32CreateFileHandle(ds, searchResult, &modeFlags);
  free(searchResult);

  return (void *) handle;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Close a previously-opened FAT32 file handle.
///
/// @details If the file was opened for writing, the directory entry on disk
///          is updated with the current file size and first-cluster fields
///          from the handle so that the on-disk metadata reflects any writes
///          that occurred while the file was open.  The handle and its
///          associated file-name buffer are then freed.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param fileHandle   Pointer to the Fat32FileHandle to close (passed as
///                     void*).
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if either
///         argument is NULL, or FAT32_ERROR on an I/O failure while flushing
///         the directory entry.
///
int fat32Fclose(void *driverState, void *fileHandle) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  int result = FAT32_SUCCESS;

  // If the file was writable, flush the authoritative size (and first-cluster
  // fields, which may have changed if the file was originally empty) back to
  // the on-disk directory entry.
  if (handle->canWrite) {
    FilesystemState    *fs = ds->filesystemState;
    BlockStorageDevice *bd = fs->blockDevice;

    // Compute the sector that contains the short directory entry.
    uint32_t sectorIndex =
      handle->directoryOffset / ds->bytesPerSector;
    uint32_t offsetInSector =
      handle->directoryOffset % ds->bytesPerSector;
    uint32_t lba =
      fat32ClusterToLba(ds, handle->directoryCluster) + sectorIndex;

    // Read the sector, patch the entry, and write it back.
    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      result = FAT32_ERROR;
    } else {
      Fat32DirectoryEntry *entry =
        (Fat32DirectoryEntry *) (fs->blockBuffer + offsetInSector);

      entry->fileSize         = handle->fileSize;
      entry->firstClusterHigh =
        (uint16_t) (handle->firstCluster >> 16);
      entry->firstClusterLow  =
        (uint16_t) (handle->firstCluster & 0xFFFF);

      ioResult = bd->writeBlocks(
        bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
      if (ioResult != 0) {
        result = FAT32_ERROR;
      }
    }
  }

  // Free the handle regardless of whether the flush succeeded — the caller
  // must not use this handle again.
  free(handle->fileName);
  free(handle);

  return result;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read data from a previously-opened FAT32 file.
///
/// @details Starting at the handle's current position, up to @p length bytes
///          are copied into the caller-supplied buffer.  The read is clamped
///          at end-of-file so that it never overruns the recorded file size.
///          The handle's currentPosition and currentCluster fields are updated
///          to reflect the new position after the read completes.
///
///          Reading is performed one sector at a time using the shared
///          blockBuffer; the cluster chain is followed transparently when the
///          current position crosses a cluster boundary.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param ptr          Destination buffer; must be at least @p length bytes.
/// @param length       Maximum number of bytes to read.
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
///
/// @return The number of bytes actually read (>= 0), or a negative FAT32
///         error code on failure.
///
int32_t fat32Fread(
    void *driverState,
    void *ptr,
    uint32_t length,
    void *fileHandle
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL) || (ptr == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  if (!handle->canRead) {
    return FAT32_INVALID_PARAMETER;
  }

  // Clamp the request to the bytes remaining between the current position and
  // end-of-file so that we never read past the logical file boundary.
  if (handle->currentPosition >= handle->fileSize) {
    return 0;
  }

  uint32_t remaining = handle->fileSize - handle->currentPosition;
  if (length > remaining) {
    length = remaining;
  }

  if (length == 0) {
    return 0;
  }

  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

  uint8_t  *dest      = (uint8_t *) ptr;
  uint32_t  totalRead = 0;

  while (totalRead < length) {
    // ---- Determine the current cluster ----
    // If currentCluster is zero the file was created empty and never had a
    // cluster allocated; there is nothing to read.
    if (handle->currentCluster < FAT32_CLUSTER_FIRST_VALID) {
      break;
    }

    // Byte offset within the current cluster.
    uint32_t offsetInCluster =
      handle->currentPosition % ds->bytesPerCluster;

    // Sector index within the cluster and byte offset within that sector.
    uint32_t sectorInCluster = offsetInCluster / ds->bytesPerSector;
    uint32_t offsetInSector  = offsetInCluster % ds->bytesPerSector;

    // Absolute sector on the block device.
    uint32_t lba =
      fat32ClusterToLba(ds, handle->currentCluster) + sectorInCluster;

    // Read one sector into the shared block buffer.
    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return (totalRead > 0) ? (int32_t) totalRead : FAT32_ERROR;
    }

    // How many usable bytes remain in this sector?
    uint32_t bytesLeftInSector = ds->bytesPerSector - offsetInSector;

    // How many bytes to copy from this sector: the minimum of the sector
    // remainder and the overall request remainder.
    uint32_t toCopy = length - totalRead;
    if (toCopy > bytesLeftInSector) {
      toCopy = bytesLeftInSector;
    }

    memcpy(dest + totalRead, fs->blockBuffer + offsetInSector, toCopy);

    totalRead                += toCopy;
    handle->currentPosition  += toCopy;

    // ---- Advance to the next cluster if we have reached the boundary ----
    if ((handle->currentPosition % ds->bytesPerCluster) == 0
        && (totalRead < length)) {
      uint32_t nextCluster;
      int fatResult =
        fat32ReadFatEntry(ds, handle->currentCluster, &nextCluster);
      if (fatResult != FAT32_SUCCESS) {
        return (totalRead > 0) ? (int32_t) totalRead : FAT32_ERROR;
      }

      // End-of-chain before the file size is reached — the FAT is
      // inconsistent, but return what we have rather than corrupting memory.
      if (nextCluster >= FAT32_CLUSTER_EOC_MIN) {
        break;
      }

      handle->currentCluster = nextCluster;
    }
  }

  return (int32_t) totalRead;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Write data to a previously-opened FAT32 file.
///
/// @details Starting at the handle's current position (or at end-of-file when
///          the file was opened in append mode), up to @p length bytes are
///          written from the caller-supplied buffer to disk.  New clusters are
///          allocated as needed when the write extends beyond the current
///          allocation.  Partial-sector writes are performed as read-modify-
///          write cycles so that surrounding data within the sector is
///          preserved.
///
///          The handle's currentPosition, currentCluster, firstCluster, and
///          fileSize fields are updated to reflect the state after the write.
///          The on-disk directory entry is NOT flushed here; that is deferred
///          to fat32Fclose so that multiple writes do not each incur the
///          overhead of a directory-entry update.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param ptr          Source buffer containing the data to write.
/// @param length       Number of bytes to write.
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
///
/// @return The number of bytes actually written (>= 0), or a negative FAT32
///         error code on failure.
///
int32_t fat32Fwrite(
    void *driverState,
    void *ptr,
    uint32_t length,
    void *fileHandle
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL) || (ptr == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  if (!handle->canWrite) {
    return FAT32_INVALID_PARAMETER;
  }

  if (length == 0) {
    return 0;
  }

  // In append mode every write begins at the current end-of-file.  Skip
  // the seek if we are already positioned there to avoid an unnecessary
  // cluster-chain walk.
  if (handle->appendMode
      && (handle->currentPosition != handle->fileSize)) {
    handle->currentPosition = handle->fileSize;

    // Ensure currentCluster points to the last cluster of the file (or
    // stays at firstCluster if the file is still empty).
    if (handle->fileSize > 0) {
      // Walk from firstCluster to the cluster that contains the last byte.
      uint32_t clustersToSkip =
        (handle->fileSize - 1) / ds->bytesPerCluster;
      uint32_t cluster = handle->firstCluster;
      for (uint32_t i = 0; i < clustersToSkip; i++) {
        uint32_t next;
        if (fat32ReadFatEntry(ds, cluster, &next) != FAT32_SUCCESS) {
          return FAT32_ERROR;
        }
        if (next >= FAT32_CLUSTER_EOC_MIN) {
          break;
        }
        cluster = next;
      }
      handle->currentCluster = cluster;
    }
  }

  FilesystemState    *fs = ds->filesystemState;
  BlockStorageDevice *bd = fs->blockDevice;

  const uint8_t *src          = (const uint8_t *) ptr;
  uint32_t       totalWritten = 0;

  while (totalWritten < length) {
    // ---- Ensure we have a cluster to write into ----
    if (handle->currentCluster < FAT32_CLUSTER_FIRST_VALID) {
      // The file has no clusters yet — allocate the first one.
      uint32_t newCluster;
      int allocResult = fat32AllocateCluster(ds, 0, &newCluster);
      if (allocResult != FAT32_SUCCESS) {
        return (totalWritten > 0) ? (int32_t) totalWritten : allocResult;
      }
      handle->firstCluster   = newCluster;
      handle->currentCluster = newCluster;
    }

    // If we are exactly on a cluster boundary and still have data to write,
    // we need the next cluster in the chain (or a freshly-allocated one).
    if ((handle->currentPosition != 0)
        && ((handle->currentPosition % ds->bytesPerCluster) == 0)) {
      uint32_t nextCluster;
      int fatResult =
        fat32ReadFatEntry(ds, handle->currentCluster, &nextCluster);
      if (fatResult != FAT32_SUCCESS) {
        return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
      }

      if (nextCluster >= FAT32_CLUSTER_EOC_MIN
          || nextCluster < FAT32_CLUSTER_FIRST_VALID) {
        // End of chain — extend with a new cluster.
        fatResult = fat32AllocateCluster(
          ds, handle->currentCluster, &nextCluster);
        if (fatResult != FAT32_SUCCESS) {
          return (totalWritten > 0) ? (int32_t) totalWritten : fatResult;
        }
      }

      handle->currentCluster = nextCluster;
    }

    // ---- Compute the sector and offset within the current cluster ----
    uint32_t offsetInCluster =
      handle->currentPosition % ds->bytesPerCluster;
    uint32_t sectorInCluster = offsetInCluster / ds->bytesPerSector;
    uint32_t offsetInSector  = offsetInCluster % ds->bytesPerSector;

    uint32_t lba =
      fat32ClusterToLba(ds, handle->currentCluster) + sectorInCluster;

    // Determine how many bytes to write within this sector.
    uint32_t bytesLeftInSector = ds->bytesPerSector - offsetInSector;
    uint32_t toCopy = length - totalWritten;
    if (toCopy > bytesLeftInSector) {
      toCopy = bytesLeftInSector;
    }

    // If the write does not cover the entire sector we must preserve the
    // existing content by reading the sector first.
    if ((offsetInSector != 0) || (toCopy < ds->bytesPerSector)) {
      int ioResult = bd->readBlocks(
        bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
      if (ioResult != 0) {
        return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
      }
    }

    // Copy the caller's data into the block buffer and write the sector.
    memcpy(fs->blockBuffer + offsetInSector, src + totalWritten, toCopy);

    int ioResult = bd->writeBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
    }

    totalWritten             += toCopy;
    handle->currentPosition  += toCopy;

    // Extend the logical file size if we have written past the old end.
    if (handle->currentPosition > handle->fileSize) {
      handle->fileSize = handle->currentPosition;
    }
  }

  return (int32_t) totalWritten;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Reposition the file-position indicator of a previously-opened FAT32
///        file.
///
/// @details The new absolute position is computed from @p offset and @p whence
///          using the standard C semantics:
///
///          SEEK_SET — offset bytes from the beginning of the file.
///          SEEK_CUR — offset bytes from the current position.
///          SEEK_END — offset bytes from the end of the file.
///
///          The resulting position is validated against the range
///          [0, fileSize]; seeking before the start of the file or past
///          end-of-file is rejected with FAT32_INVALID_PARAMETER.
///
///          After the position is updated the cluster chain is walked from
///          firstCluster so that currentCluster points to the cluster
///          containing the target byte.  When the new position falls exactly
///          on a cluster boundary at end-of-file, currentCluster is set to
///          the last allocated cluster and the read/write loops are relied
///          upon to advance the chain at the start of their next iteration.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
/// @param offset       Signed byte offset relative to @p whence.
/// @param whence       One of SEEK_SET, SEEK_CUR, or SEEK_END.
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if an argument
///         is NULL, whence is unrecognised, or the computed position is out of
///         range, or FAT32_ERROR on an I/O failure while walking the cluster
///         chain.
///
int fat32Fseek(
    void *driverState,
    void *fileHandle,
    long offset,
    int whence
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Determine the base for the offset ----
  long base;
  switch (whence) {
    case SEEK_SET: base = 0;                              break;
    case SEEK_CUR: base = (long) handle->currentPosition; break;
    case SEEK_END: base = (long) handle->fileSize;        break;
    default:       return FAT32_INVALID_PARAMETER;
  }

  long newPosition = base + offset;

  // Reject positions outside [0, fileSize].
  if (newPosition < 0) {
    return FAT32_INVALID_PARAMETER;
  }
  if ((uint32_t) newPosition > handle->fileSize) {
    return FAT32_INVALID_PARAMETER;
  }

  uint32_t pos = (uint32_t) newPosition;
  handle->currentPosition = pos;

  // ---- Synchronise currentCluster with the new position ----
  //
  // If the file has no clusters (empty and never written to) or the new
  // position is zero, reset to firstCluster — no chain walk needed.
  if ((pos == 0)
      || (handle->firstCluster < FAT32_CLUSTER_FIRST_VALID)) {
    handle->currentCluster = handle->firstCluster;
  } else {
    // Compute how many cluster links to follow from firstCluster.
    //
    // When the target position sits exactly at end-of-file AND falls on a
    // cluster boundary, using (fileSize - 1) avoids following the chain
    // past the last allocated cluster.  The read and write loops handle
    // the boundary transition at the start of their next iteration.
    uint32_t clustersToSkip;
    if (pos >= handle->fileSize) {
      clustersToSkip = (handle->fileSize - 1) / ds->bytesPerCluster;
    } else {
      clustersToSkip = pos / ds->bytesPerCluster;
    }

    uint32_t cluster = handle->firstCluster;
    for (uint32_t i = 0; i < clustersToSkip; i++) {
      uint32_t next;
      if (fat32ReadFatEntry(ds, cluster, &next) != FAT32_SUCCESS) {
        return FAT32_ERROR;
      }
      if (next >= FAT32_CLUSTER_EOC_MIN) {
        break;
      }
      cluster = next;
    }
    handle->currentCluster = cluster;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Remove (delete) a file from a FAT32 filesystem.
///
/// @details The full path is resolved to locate the file's parent directory
///          and file name.  If the target exists and is a regular file (not a
///          directory), its cluster chain is freed and the on-disk directory
///          entries — both the short (8.3) entry and any preceding LFN
///          entries — are marked as deleted (first byte set to 0xE5) so that
///          the space can be reclaimed by future allocations.
///
///          This function does not check whether the file is currently open.
///          The caller (typically the filesystem task) is responsible for
///          ensuring that no open handles reference the file being removed.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param pathname     The null-terminated path of the file to remove.
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if either
///         argument is NULL or the target is a directory, FAT32_FILE_NOT_FOUND
///         if the file (or an intermediate directory) does not exist, or
///         FAT32_ERROR on an I/O failure.
///
int fat32Remove(void *driverState, const char *pathname) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (pathname == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Resolve the parent directory and extract the file name ----
  uint32_t    parentCluster;
  const char *fileName = NULL;

  int result = fat32ResolveParentDirectory(
    ds, pathname, &parentCluster, &fileName);
  if (result != FAT32_SUCCESS) {
    return result;
  }

  if ((fileName == NULL) || (fileName[0] == '\0')) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Search for the file in the parent directory ----
  Fat32DirSearchResult *searchResult =
    (Fat32DirSearchResult *) malloc(sizeof(Fat32DirSearchResult));
  if (searchResult == NULL) {
    return FAT32_NO_MEMORY;
  }

  result = fat32SearchDirectory(
    ds, parentCluster, fileName, searchResult);
  if (result != FAT32_SUCCESS) {
    free(searchResult);
    return result;
  }

  // Directories must not be removed with this call (use a dedicated rmdir
  // operation if one is added later).
  if (searchResult->entry.attributes & FAT32_ATTR_DIRECTORY) {
    free(searchResult);
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Free the file's cluster chain ----
  uint32_t firstCluster =
    ((uint32_t) searchResult->entry.firstClusterHigh << 16)
    | (uint32_t) searchResult->entry.firstClusterLow;

  if (firstCluster >= FAT32_CLUSTER_FIRST_VALID) {
    result = fat32FreeClusterChain(ds, firstCluster);
    if (result != FAT32_SUCCESS) {
      free(searchResult);
      return result;
    }
  }

  // ---- Invalidate the directory entries (LFN + short) ----
  result = fat32InvalidateDirectoryEntries(
    ds, parentCluster, searchResult);

  free(searchResult);
  return result;
}
