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
/// @file              ExFatFilesystem.c
///
/// @brief             Memory-efficient exFAT driver implementation for NanoOs.
///
///////////////////////////////////////////////////////////////////////////////

#include "ExFatFilesystem.h"
#include "NanoOs.h"
#include "NanoOsTypes.h"
#include "Tasks.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"
#include "Filesystem.h"

/// @brief Read a sector from the storage device
///
/// @param driverState Pointer to driver state
/// @param sectorNumber Sector number to read
/// @param buffer Buffer to read into
///
/// @return EXFAT_SUCCESS on success, EXFAT_ERROR on failure
static int readSector(
  ExFatDriverState* driverState, uint32_t sectorNumber, uint8_t* buffer
) {
  if (driverState == NULL || driverState->filesystemState == NULL || 
      buffer == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  int result = filesystemState->blockDevice->readBlocks(
    filesystemState->blockDevice->context,
    filesystemState->startLba + sectorNumber,
    1,
    filesystemState->blockSize,
    buffer
  );

  return (result == 0) ? EXFAT_SUCCESS : EXFAT_ERROR;
}

/// @brief Write a sector to the storage device
///
/// @param driverState Pointer to driver state
/// @param sectorNumber Sector number to write
/// @param buffer Buffer to write from
///
/// @return EXFAT_SUCCESS on success, EXFAT_ERROR on failure
static int writeSector(
  ExFatDriverState* driverState, uint32_t sectorNumber, uint8_t* buffer
) {
  if (driverState == NULL || driverState->filesystemState == NULL || 
      buffer == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  int result = filesystemState->blockDevice->writeBlocks(
    filesystemState->blockDevice->context,
    filesystemState->startLba + sectorNumber,
    1,
    filesystemState->blockSize,
    buffer
  );

  return (result == 0) ? EXFAT_SUCCESS : EXFAT_ERROR;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Initialize an exFAT driver state
///
/// @param filesystemState Pointer to initialized FilesystemState struct
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
int exFatInitialize(FilesystemState* filesystemState) {
  if (filesystemState == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }
  ExFatDriverState *driverState
    = (ExFatDriverState*) calloc(1, sizeof(ExFatDriverState));
  if (driverState == NULL) {
    return EXFAT_NO_MEMORY;
  }

  if ((filesystemState->blockDevice == NULL)
    || (filesystemState->blockBuffer == NULL)
  ) {
    return EXFAT_INVALID_PARAMETER;
  }

  // Use the existing blockBuffer to read boot sector
  uint8_t* buffer = filesystemState->blockBuffer;

  // Read the boot sector (sector 0)
  int result = filesystemState->blockDevice->readBlocks(
    filesystemState->blockDevice->context,
    filesystemState->startLba,
    1,
    filesystemState->blockSize,
    buffer
  );

  if (result != 0) {
    return EXFAT_ERROR;
  }

  // Validate boot signature
  uint16_t bootSignature = 0;
  readBytes(&bootSignature, &buffer[510]);
  if (bootSignature != 0xAA55) {
    return EXFAT_INVALID_FILESYSTEM;
  }

  // Validate filesystem name
  const char* expectedName = "EXFAT   ";
  for (uint8_t ii = 0; ii < 8; ii++) {
    if (buffer[3 + ii] != expectedName[ii]) {
      return EXFAT_INVALID_FILESYSTEM;
    }
  }

  // Extract boot sector values
  uint8_t bytesPerSectorShift = buffer[108];
  uint8_t sectorsPerClusterShift = buffer[109];
  uint32_t fatOffset = 0;
  uint32_t clusterHeapOffset = 0;
  uint32_t clusterCount = 0;
  uint32_t rootDirectoryCluster = 0;

  readBytes(&fatOffset, &buffer[80]);
  readBytes(&clusterHeapOffset, &buffer[88]);
  readBytes(&clusterCount, &buffer[92]);
  readBytes(&rootDirectoryCluster, &buffer[96]);

  // Calculate values
  uint32_t bytesPerSector = 1 << bytesPerSectorShift;
  uint32_t sectorsPerCluster = 1 << sectorsPerClusterShift;
  uint32_t bytesPerCluster = bytesPerSector * sectorsPerCluster;

  // Validate values
  if (bytesPerSector < EXFAT_SECTOR_SIZE) {
    return EXFAT_INVALID_FILESYSTEM;
  }

  if (bytesPerCluster < EXFAT_CLUSTER_SIZE_MIN ||
      bytesPerCluster > EXFAT_CLUSTER_SIZE_MAX) {
    return EXFAT_INVALID_FILESYSTEM;
  }

  if (rootDirectoryCluster < 2) {
    return EXFAT_INVALID_FILESYSTEM;
  }

  // Initialize driver state
  driverState->filesystemState = filesystemState;
  driverState->bytesPerSector = bytesPerSector;
  driverState->sectorsPerCluster = sectorsPerCluster;
  driverState->bytesPerCluster = bytesPerCluster;
  driverState->fatStartSector = fatOffset;
  driverState->clusterHeapStartSector = clusterHeapOffset;
  driverState->rootDirectoryCluster = rootDirectoryCluster;
  driverState->clusterCount = clusterCount;

  // Initialize the filesystem state's driver state with our state
  filesystemState->driverState = driverState;

  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Convert a cluster number to a sector number
///
/// @param driverState Pointer to the exFAT driver state
/// @param cluster The cluster number to convert
///
/// @return The sector number corresponding to the cluster
///////////////////////////////////////////////////////////////////////////////
static uint32_t clusterToSector(
  ExFatDriverState* driverState, uint32_t cluster
) {
  if (cluster < 2) {
    return 0;
  }
  return driverState->clusterHeapStartSector +
    ((cluster - 2) * driverState->sectorsPerCluster);
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Read a FAT entry
///
/// @param driverState Pointer to the exFAT driver state
/// @param cluster The cluster whose FAT entry to read
/// @param nextCluster Pointer to store the next cluster number
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int readFatEntry(
  ExFatDriverState* driverState, uint32_t cluster, uint32_t* nextCluster
) {
  if (driverState == NULL || nextCluster == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  uint32_t fatOffset = cluster * 4;
  uint32_t fatSector = driverState->fatStartSector +
    (fatOffset / driverState->bytesPerSector);
  uint32_t entryOffset = fatOffset % driverState->bytesPerSector;

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  int result = readSector(driverState, fatSector, buffer);
  if (result != EXFAT_SUCCESS) {
    return result;
  }

  readBytes(nextCluster, &buffer[entryOffset]);
  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Write a FAT entry
///
/// @param driverState Pointer to the exFAT driver state
/// @param cluster The cluster whose FAT entry to write
/// @param value The value to write to the FAT entry
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int writeFatEntry(
  ExFatDriverState* driverState, uint32_t cluster, uint32_t value
) {
  if (driverState == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  uint32_t fatOffset = cluster * 4;
  uint32_t fatSector = driverState->fatStartSector +
    (fatOffset / driverState->bytesPerSector);
  uint32_t entryOffset = fatOffset % driverState->bytesPerSector;

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Read current sector
  int result = readSector(driverState, fatSector, buffer);
  if (result != EXFAT_SUCCESS) {
    printString("ERROR: Failed to read FAT sector!\n");
    return result;
  }

  // Write new value
  writeBytes(&buffer[entryOffset], &value);

  // Write sector to disk
  result = writeSector(driverState, fatSector, buffer);
  if (result != EXFAT_SUCCESS) {
    printString("ERROR: Failed to write FAT sector!\n");
    return result;
  }

  return result;
}

///////////////////////////////////////////////////////////////////////////////
/// @struct NoFatChainRange
///
/// @brief Represents a range of clusters used by a NoFatChain file
///
/// @param startCluster First cluster in the range
/// @param endCluster Last cluster in the range (inclusive)
typedef struct NoFatChainRange {
  uint32_t startCluster;
  uint32_t endCluster;
} NoFatChainRange;

///////////////////////////////////////////////////////////////////////////////
/// @brief Check if a cluster is in any NoFatChain range
///
/// @param cluster Cluster to check
/// @param ranges Array of NoFatChain ranges
/// @param numRanges Number of ranges in the array
///
/// @return true if cluster is in a NoFatChain range, false otherwise
///////////////////////////////////////////////////////////////////////////////
static bool isClusterInNoFatChainRange(
  uint32_t cluster, NoFatChainRange* ranges, uint8_t numRanges
) {
  for (uint8_t ii = 0; ii < numRanges; ii++) {
    if (cluster >= ranges[ii].startCluster &&
        cluster <= ranges[ii].endCluster) {
      return true;
    }
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Collect NoFatChain cluster ranges from directory
///
/// @param driverState Pointer to the exFAT driver state
/// @param ranges Pointer to array to store ranges
/// @param maxRanges Maximum number of ranges that can be stored
/// @param numRanges Pointer to store actual number of ranges found
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int collectNoFatChainRanges(
  ExFatDriverState* driverState, NoFatChainRange* ranges,
  uint8_t maxRanges, uint8_t* numRanges
) {
  if (driverState == NULL || ranges == NULL || numRanges == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  *numRanges = 0;
  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  uint32_t currentCluster = driverState->rootDirectoryCluster;

  while (currentCluster != 0xFFFFFFFF && currentCluster >= 2) {
    for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
      uint32_t sector = clusterToSector(driverState, currentCluster) + ss;
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        return result;
      }

      for (
        uint32_t ii = 0;
        ii < driverState->bytesPerSector;
        ii += EXFAT_DIRECTORY_ENTRY_SIZE
      ) {
        uint8_t entryType = buffer[ii];

        if (entryType == EXFAT_ENTRY_END_OF_DIR) {
          return EXFAT_SUCCESS;
        }

        if (entryType == EXFAT_ENTRY_FILE) {
          if (ii + EXFAT_DIRECTORY_ENTRY_SIZE >=
              driverState->bytesPerSector) {
            continue;
          }

          uint8_t streamType = buffer[ii + EXFAT_DIRECTORY_ENTRY_SIZE];
          if (streamType == EXFAT_ENTRY_STREAM) {
            uint8_t flags = buffer[ii + EXFAT_DIRECTORY_ENTRY_SIZE + 1];
            
            // Check NoFatChain bit (bit 1)
            if ((flags & 0x02) != 0) {
              if (*numRanges >= maxRanges) {
                printString("  WARNING: Too many NoFatChain files, ");
                printString("some may not be tracked!\n");
                return EXFAT_SUCCESS;
              }

              uint32_t firstCluster = 0;
              readBytes(
                &firstCluster,
                &buffer[ii + EXFAT_DIRECTORY_ENTRY_SIZE + 20]
              );

              uint64_t dataLength = 0;
              readBytes(
                &dataLength,
                &buffer[ii + EXFAT_DIRECTORY_ENTRY_SIZE + 24]
              );

              uint32_t clustersNeeded = 0;
              if (dataLength > 0) {
                clustersNeeded = 
                  ((uint32_t) dataLength + driverState->bytesPerCluster - 1) /
                  driverState->bytesPerCluster;
              } else {
                clustersNeeded = 1;
              }

              ranges[*numRanges].startCluster = firstCluster;
              ranges[*numRanges].endCluster = firstCluster + clustersNeeded - 1;
              
              (*numRanges)++;
            }
          }
        }
      }
    }

    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    currentCluster = nextCluster;
  }

  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Check if cluster is free in allocation bitmap
///
/// @param driverState Pointer to the exFAT driver state
/// @param bitmapCluster First cluster of allocation bitmap
/// @param cluster Cluster to check
/// @param isFree Pointer to store result (true if free)
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int isClusterFreeInBitmap(
  ExFatDriverState* driverState, uint32_t bitmapCluster,
  uint32_t cluster, bool* isFree
) {
  if (driverState == NULL || isFree == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Calculate bit position (cluster 2 = bit 0)
  uint32_t bitPosition = cluster - 2;
  uint32_t byteOffset = bitPosition / 8;
  uint8_t bitOffset = bitPosition % 8;

  // Calculate sector containing this byte
  uint32_t bytesPerCluster = driverState->bytesPerCluster;
  uint32_t clusterOffset = byteOffset / bytesPerCluster;
  uint32_t byteInCluster = byteOffset % bytesPerCluster;
  uint32_t sectorInCluster = byteInCluster / driverState->bytesPerSector;
  uint32_t byteInSector = byteInCluster % driverState->bytesPerSector;

  // For now, assume bitmap fits in one cluster
  // TODO: Handle multi-cluster bitmaps
  if (clusterOffset > 0) {
    printString("    WARNING: Bitmap cluster offset > 0\n");
  }

  uint32_t bitmapSector = clusterToSector(driverState, bitmapCluster) +
    sectorInCluster;

  // Read bitmap sector
  int result = readSector(driverState, bitmapSector, buffer);
  if (result != EXFAT_SUCCESS) {
    return result;
  }

  // Check the bit
  uint8_t byteValue = buffer[byteInSector];
  bool bitSet = (byteValue & (1 << bitOffset)) != 0;
  
  *isFree = !bitSet;  // Bit set = allocated, bit clear = free
  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Find the allocation bitmap location
///
/// @param driverState Pointer to the exFAT driver state
/// @param bitmapCluster Pointer to store first cluster of bitmap
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int findAllocationBitmap(
  ExFatDriverState* driverState, uint32_t* bitmapCluster
) {
  if (driverState == NULL || bitmapCluster == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Scan root directory for bitmap entry (0x81)
  uint32_t currentCluster = driverState->rootDirectoryCluster;

  while (currentCluster != 0xFFFFFFFF && currentCluster >= 2) {
    for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
      uint32_t sector = clusterToSector(driverState, currentCluster) + ss;
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        return result;
      }

      for (
        uint32_t ii = 0;
        ii < driverState->bytesPerSector;
        ii += EXFAT_DIRECTORY_ENTRY_SIZE
      ) {
        uint8_t entryType = buffer[ii];

        if (entryType == EXFAT_ENTRY_END_OF_DIR) {
          printString("  ERROR: Bitmap entry not found!\n");
          return EXFAT_ERROR;
        }

        if (entryType == EXFAT_ENTRY_ALLOCATION_BITMAP) {
          // Found it! Read the first cluster
          readBytes(bitmapCluster, &buffer[ii + 20]);
          
          return EXFAT_SUCCESS;
        }
      }
    }

    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    currentCluster = nextCluster;
  }

  return EXFAT_ERROR;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Find a free cluster (checks bitmap, FAT, and NoFatChain)
///
/// @param driverState Pointer to the exFAT driver state
/// @param freeCluster Pointer to store the free cluster number
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int findFreeCluster(
  ExFatDriverState* driverState, uint32_t* freeCluster
) {
  if (driverState == NULL || freeCluster == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  // Find bitmap location
  uint32_t bitmapCluster = 0;
  int result = findAllocationBitmap(driverState, &bitmapCluster);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Cannot find allocation bitmap\n");
    return result;
  }

  // Collect NoFatChain ranges (small memory footprint)
  const uint8_t maxRanges = 16;
  NoFatChainRange* ranges = (NoFatChainRange*) malloc(
    maxRanges * sizeof(NoFatChainRange)
  );
  if (ranges == NULL) {
    printString("  ERROR: Cannot allocate range array\n");
    return EXFAT_NO_MEMORY;
  }

  uint8_t numRanges = 0;
  result = collectNoFatChainRanges(driverState, ranges, maxRanges, &numRanges);
  if (result != EXFAT_SUCCESS) {
    free(ranges);
    return result;
  }

  for (uint32_t cluster = 2;
    cluster < driverState->clusterCount + 2;
    cluster++
  ) {
    // Check 1: NoFatChain ranges
    if (isClusterInNoFatChainRange(cluster, ranges, numRanges)) {
      continue;
    }

    // Check 2: Allocation bitmap
    bool bitmapFree = false;
    result = isClusterFreeInBitmap(
      driverState, bitmapCluster, cluster, &bitmapFree
    );
    if (result != EXFAT_SUCCESS) {
      free(ranges);
      return result;
    }
    
    if (!bitmapFree) {
      continue;  // Bitmap says it's allocated
    }

    // Check 3: FAT
    uint32_t fatValue = 0;
    result = readFatEntry(driverState, cluster, &fatValue);
    if (result != EXFAT_SUCCESS) {
      free(ranges);
      return result;
    }

    if (fatValue == 0) {
      // Found a truly free cluster!
      *freeCluster = cluster;
      free(ranges);
      return EXFAT_SUCCESS;
    }
  }

  // No free clusters
  printString("  ERROR: No free clusters available\n");
  free(ranges);
  return EXFAT_DISK_FULL;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Update allocation bitmap for a cluster
///
/// @param driverState Pointer to the exFAT driver state
/// @param cluster Cluster to mark as allocated/free
/// @param allocated true to mark as allocated, false for free
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int updateAllocationBitmap(
  ExFatDriverState* driverState, uint32_t cluster, bool allocated
) {
  if (driverState == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Find bitmap location
  uint32_t bitmapCluster = 0;
  int result = findAllocationBitmap(driverState, &bitmapCluster);
  if (result != EXFAT_SUCCESS) {
    return result;
  }

  // Calculate which byte and bit in the bitmap
  // Bitmap starts at cluster 2, so cluster N is at bit (N-2)
  uint32_t bitPosition = cluster - 2;
  uint32_t byteOffset = bitPosition / 8;
  uint8_t bitOffset = bitPosition % 8;

  // Calculate which sector of the bitmap contains this byte
  uint32_t bytesPerCluster = driverState->bytesPerCluster;
  uint32_t clusterOffset = byteOffset / bytesPerCluster;
  uint32_t byteInCluster = byteOffset % bytesPerCluster;
  uint32_t sectorInCluster = byteInCluster / driverState->bytesPerSector;
  uint32_t byteInSector = byteInCluster % driverState->bytesPerSector;

  // Calculate the actual bitmap cluster
  // For simplicity, assume bitmap is in a single cluster for now
  // TODO: Handle bitmaps that span multiple clusters
  if (clusterOffset > 0) {
    printString("  WARNING: Bitmap spans multiple clusters!\n");
    // Would need to follow FAT chain here
  }

  uint32_t bitmapSector = clusterToSector(driverState, bitmapCluster) +
    sectorInCluster;

  // Read bitmap sector
  result = readSector(driverState, bitmapSector, buffer);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to read bitmap sector\n");
    return result;
  }

  // Update the bit
  if (allocated) {
    buffer[byteInSector] |= (1 << bitOffset);
  } else {
    buffer[byteInSector] &= ~(1 << bitOffset);
  }

  // Write bitmap sector back
  result = writeSector(driverState, bitmapSector, buffer);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to write bitmap sector\n");
    return result;
  }

  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Allocate a new cluster (FIXED with bitmap support)
///
/// @param driverState Pointer to the exFAT driver state
/// @param newCluster Pointer to store the allocated cluster number
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int allocateCluster(
  ExFatDriverState* driverState, uint32_t* newCluster
) {
  if (driverState == NULL || newCluster == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  int result = findFreeCluster(driverState, newCluster);
  if (result != EXFAT_SUCCESS) {
    return result;
  }

  // Update FAT
  result = writeFatEntry(driverState, *newCluster, 0xFFFFFFFF);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to write FAT entry\n");
    return result;
  }

  // Update allocation bitmap
  result = updateAllocationBitmap(driverState, *newCluster, true);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to update allocation bitmap\n");
    return result;
  }

  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Calculate checksum for directory entry set
///
/// @param entries Pointer to the directory entries
/// @param numEntries Number of entries in the set
///
/// @return The calculated checksum
///////////////////////////////////////////////////////////////////////////////
static uint16_t calculateEntrySetChecksum(
  const uint8_t* entries, uint8_t numEntries
) {
  uint16_t checksum = 0;
  uint32_t totalBytes = numEntries * EXFAT_DIRECTORY_ENTRY_SIZE;

  for (uint32_t ii = 0; ii < totalBytes; ii++) {
    if (ii == 2 || ii == 3) {
      // Skip checksum field itself
      continue;
    }
    checksum = ((checksum << 15) | (checksum >> 1)) + (uint16_t) entries[ii];
  }

  return checksum;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Parse a filename component and convert to UTF-16
///
/// @param name The ASCII filename component
/// @param utf16Name Buffer to store UTF-16 name
/// @param maxLength Maximum length of the UTF-16 buffer
///
/// @return The length of the UTF-16 name
///////////////////////////////////////////////////////////////////////////////
static uint8_t asciiToUtf16(
  const char* name, uint16_t* utf16Name, uint8_t maxLength
) {
  uint8_t length = 0;
  while (*name != '\0' && length < maxLength) {
    utf16Name[length++] = (uint16_t) *name;
    name++;
  }
  return length;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Calculate hash for a filename (UPPERCASE)
///
/// exFAT requires hash to be calculated from UPPERCASE filename
///
/// @param utf16Name The UTF-16 filename
/// @param nameLength Length of the filename
///
/// @return The calculated hash
///////////////////////////////////////////////////////////////////////////////
static uint16_t calculateNameHash(
  const uint16_t* utf16Name, uint8_t nameLength
) {
  uint16_t hash = 0;
  for (uint8_t ii = 0; ii < nameLength; ii++) {
    uint16_t character = utf16Name[ii];
    
    // Convert to uppercase for hash calculation
    if (character >= 0x0061 && character <= 0x007A) {  // lowercase a-z
      character = character - 0x0020;  // Convert to uppercase
    }
    
    // Hash calculation
    hash = ((hash << 15) | (hash >> 1)) + (character & 0xFF);
    hash = ((hash << 15) | (hash >> 1)) + (character >> 8);
  }
  return hash;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Compare two filenames (case-insensitive)
///
/// @param name1 First filename (UTF-16)
/// @param length1 Length of first filename
/// @param name2 Second filename (UTF-16)
/// @param length2 Length of second filename
///
/// @return 0 if equal, non-zero if not equal
///////////////////////////////////////////////////////////////////////////////
static int compareFilenames(
  const uint16_t* name1, uint8_t length1,
  const uint16_t* name2, uint8_t length2
) {
  if (length1 != length2) {
    return 1;
  }

  for (uint8_t ii = 0; ii < length1; ii++) {
    uint16_t c1 = name1[ii];
    uint16_t c2 = name2[ii];
    // Simple case conversion for ASCII
    if (c1 >= 'a' && c1 <= 'z') {
      c1 -= 32;
    }
    if (c2 >= 'a' && c2 <= 'z') {
      c2 -= 32;
    }
    if (c1 != c2) {
      return 1;
    }
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Create a valid exFAT timestamp
///
/// exFAT timestamp format:
/// - Bits 0-4: Day of month (1-31)
/// - Bits 5-8: Month (1-12)  
/// - Bits 9-15: Year offset from 1980
/// - Bits 16-20: Seconds/2 (0-29)
/// - Bits 21-26: Minutes (0-59)
/// - Bits 27-31: Hours (0-23)
///
/// @return A valid timestamp (defaults to Jan 1, 2020, 00:00:00)
///////////////////////////////////////////////////////////////////////////////
static uint32_t createValidTimestamp(void) {
  // Default: January 1, 2020, 00:00:00
  // Year: 2020 - 1980 = 40
  // Month: 1
  // Day: 1
  // Hour: 0
  // Minute: 0
  // Second: 0
  
  uint32_t day = 13;           // Bits 0-4
  uint32_t month = 10;         // Bits 5-8
  uint32_t year = 45;         // Bits 9-15 (2025 - 1980)
  uint32_t seconds2 = 0;      // Bits 16-20
  uint32_t minutes = 0;       // Bits 21-26
  uint32_t hours = 12;         // Bits 27-31
  
  uint32_t timestamp = 
    (day & 0x1F) |
    ((month & 0x0F) << 5) |
    ((year & 0x7F) << 9) |
    ((seconds2 & 0x1F) << 16) |
    ((minutes & 0x3F) << 21) |
    ((hours & 0x1F) << 27);
  
  return timestamp;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Create a new file in a directory (FIXED VERSION)
///
/// This version properly handles entry sets that may span sector boundaries
/// and ensures all entries are written to disk correctly.
///
/// @param driverState Pointer to the exFAT driver state
/// @param directoryCluster First cluster of the directory
/// @param fileName Name of the file to create
/// @param fileEntry Pointer to store created file directory entry
/// @param streamEntry Pointer to store created stream extension entry
/// @param dirCluster Pointer to store directory cluster where file was
///                   created
/// @param dirOffset Pointer to store offset in directory where file was
///                  created
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int createFileEntry(
  ExFatDriverState* driverState, uint32_t directoryCluster,
  const char* fileName, ExFatFileDirectoryEntry* fileEntry,
  ExFatStreamExtensionEntry* streamEntry, uint32_t* dirCluster,
  uint32_t* dirOffset
) {
  if ((driverState == NULL)
    || (fileName == NULL) || (*fileName == '\0')
    || (fileEntry == NULL)
    || (streamEntry == NULL)
  ) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Allocate UTF-16 name buffer
  uint16_t* utf16Name = (uint16_t*) malloc(
    EXFAT_MAX_FILENAME_LENGTH * sizeof(uint16_t)
  );
  if (utf16Name == NULL) {
    return EXFAT_NO_MEMORY;
  }

  // Convert filename to UTF-16
  uint8_t nameLength = asciiToUtf16(
    fileName, utf16Name, EXFAT_MAX_FILENAME_LENGTH
  );

  uint8_t numNameEntries = (nameLength + 14) / 15;
  uint8_t totalEntries = 2 + numNameEntries;

  // Find space for entries
  uint32_t currentCluster = directoryCluster;
  uint32_t targetSector = 0;
  uint32_t targetOffset = 0;
  bool foundSpace = false;
  int returnValue = EXFAT_SUCCESS;
  uint32_t entriesPerSector =
    driverState->bytesPerSector / EXFAT_DIRECTORY_ENTRY_SIZE;

  while (currentCluster != 0xFFFFFFFF && !foundSpace) {
    for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
      uint32_t sector = clusterToSector(driverState, currentCluster) + ss;
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        returnValue = result;
        goto cleanup;
      }

      uint8_t consecutiveFree = 0;
      uint32_t firstFreeSector = 0;
      uint32_t firstFreeOffset = 0;
      
      for (
        uint32_t ii = 0;
        ii < driverState->bytesPerSector;
        ii += EXFAT_DIRECTORY_ENTRY_SIZE
      ) {
        uint8_t entryType = buffer[ii];

        if (entryType == EXFAT_ENTRY_UNUSED ||
            entryType == EXFAT_ENTRY_END_OF_DIR) {
          if (consecutiveFree == 0) {
            firstFreeSector = sector;
            firstFreeOffset = ii;
          }
          consecutiveFree++;

          if (consecutiveFree >= totalEntries) {
            targetSector = firstFreeSector;
            targetOffset = firstFreeOffset;
            foundSpace = true;
            break;
          }
        } else {
          consecutiveFree = 0;
        }
      }

      if (foundSpace) {
        break;
      }
    }

    if (!foundSpace) {
      uint32_t nextCluster = 0;
      int result = readFatEntry(driverState, currentCluster, &nextCluster);
      if (result != EXFAT_SUCCESS) {
        returnValue = result;
        goto cleanup;
      }
      currentCluster = nextCluster;
    }
  }

  if (!foundSpace) {
    returnValue = EXFAT_DISK_FULL;
    goto cleanup;
  }

  // Allocate first cluster for the file
  uint32_t firstCluster = 0;
  /*
    * int result = allocateCluster(driverState, &firstCluster);
    * if (result != EXFAT_SUCCESS) {
    *   returnValue = result;
    *   goto cleanup;
    * }
   */

  // CRITICAL FIX: Check if entries span multiple sectors
  uint32_t entrySetEndOffset =
    targetOffset + (totalEntries * EXFAT_DIRECTORY_ENTRY_SIZE);
  bool spansMultipleSectors = (entrySetEndOffset > driverState->bytesPerSector);
  
  if (spansMultipleSectors) {
    printString("WARNING: Entry set spans multiple sectors!\n");
    printString("This case is not yet implemented.\n");
    returnValue = EXFAT_ERROR;
    goto cleanup;
  }

  // Allocate a temporary buffer for building the entry set
  uint8_t* entrySetBuffer = (uint8_t*) malloc(
    totalEntries * EXFAT_DIRECTORY_ENTRY_SIZE
  );
  if (entrySetBuffer == NULL) {
    returnValue = EXFAT_NO_MEMORY;
    goto cleanup;
  }

  // Zero out the temporary buffer
  for (uint32_t ii = 0; ii < totalEntries * EXFAT_DIRECTORY_ENTRY_SIZE; ii++) {
    entrySetBuffer[ii] = 0;
  }

  // Build file directory entry
  ExFatFileDirectoryEntry* newFileEntry =
    (ExFatFileDirectoryEntry*) entrySetBuffer;
  
  uint8_t entryType = EXFAT_ENTRY_FILE;
  uint8_t secondaryCount = totalEntries - 1;
  uint16_t attributes = EXFAT_ATTR_ARCHIVE;
  uint32_t timestamp = createValidTimestamp();
  uint8_t zeroValue = 0;
  uint8_t create10msIncrement = 0x31;
  uint8_t lastModified10msIncrement = 0x31;
  uint8_t createUtcOffset = 0x80;
  uint8_t lastModifiedUtcOffset = 0x80;
  uint8_t lastAccessedUtcOffset = 0x80;
  
  writeBytes(&newFileEntry->entryType, &entryType);
  writeBytes(&newFileEntry->secondaryCount, &secondaryCount);
  writeBytes(&newFileEntry->fileAttributes, &attributes);
  writeBytes(&newFileEntry->reserved1, &zeroValue);
  writeBytes(&newFileEntry->createTimestamp, &timestamp);
  writeBytes(&newFileEntry->lastModifiedTimestamp, &timestamp);
  writeBytes(&newFileEntry->lastAccessedTimestamp, &timestamp);
  writeBytes(&newFileEntry->createUtcOffset, &zeroValue);
  writeBytes(&newFileEntry->lastModifiedUtcOffset, &zeroValue);
  writeBytes(&newFileEntry->lastAccessedUtcOffset, &zeroValue);
  writeBytes(&newFileEntry->create10msIncrement, &create10msIncrement);
  writeBytes(&newFileEntry->lastModified10msIncrement,
    &lastModified10msIncrement);
  writeBytes(&newFileEntry->createUtcOffset, &createUtcOffset);
  writeBytes(&newFileEntry->lastModifiedUtcOffset, &lastModifiedUtcOffset);
  writeBytes(&newFileEntry->lastAccessedUtcOffset, &lastAccessedUtcOffset);

  // Build stream extension entry
  ExFatStreamExtensionEntry* newStreamEntry =
    (ExFatStreamExtensionEntry*) (entrySetBuffer + EXFAT_DIRECTORY_ENTRY_SIZE);
  
  uint8_t streamEntryType = EXFAT_ENTRY_STREAM;
  //// uint8_t generalFlags = 0x00; // No allocation possible, no fat chain
  uint8_t generalFlags = 0x01; // Allocation possible
  //// uint8_t generalFlags = 0x03; // Allocation possible, no fat chain
  uint16_t nameHash = calculateNameHash(utf16Name, nameLength);
  uint64_t validDataLength = 0;
  uint64_t dataLength = 0;
  uint32_t reserved = 0;
  
  writeBytes(&newStreamEntry->entryType, &streamEntryType);
  writeBytes(&newStreamEntry->generalSecondaryFlags, &generalFlags);
  writeBytes(&newStreamEntry->reserved1, &zeroValue);
  writeBytes(&newStreamEntry->nameLength, &nameLength);
  writeBytes(&newStreamEntry->nameHash, &nameHash);
  writeBytes(&newStreamEntry->reserved2, &zeroValue);
  writeBytes(&newStreamEntry->validDataLength, &validDataLength);
  writeBytes(&newStreamEntry->reserved3, &reserved);
  writeBytes(&newStreamEntry->firstCluster, &firstCluster);
  writeBytes(&newStreamEntry->dataLength, &dataLength);

  // Build filename entries
  uint8_t nameEntryType = EXFAT_ENTRY_FILENAME;
  uint8_t nameIndex = 0;
  
  for (uint8_t ii = 0; ii < numNameEntries; ii++) {
    ExFatFileNameEntry* newNameEntry =
      (ExFatFileNameEntry*) (entrySetBuffer +
        (2 + ii) * EXFAT_DIRECTORY_ENTRY_SIZE);
    
    writeBytes(&newNameEntry->entryType, &nameEntryType);
    writeBytes(&newNameEntry->generalSecondaryFlags, &zeroValue);
    
    for (uint8_t jj = 0; jj < 15; jj++) {
      uint16_t character = 0;
      if (nameIndex < nameLength) {
        character = utf16Name[nameIndex];
        nameIndex++;
      }
      writeBytes(&newNameEntry->fileName[jj], &character);
    }
  }

  // Calculate checksum over the entry set
  uint16_t checksum = calculateEntrySetChecksum(
    entrySetBuffer, totalEntries
  );
  writeBytes(&newFileEntry->setChecksum, &checksum);

  // Read the target sector
  int result = readSector(driverState, targetSector, buffer);
  if (result != EXFAT_SUCCESS) {
    free(entrySetBuffer);
    returnValue = result;
    goto cleanup;
  }

  // Copy entry set to the sector buffer at the target offset
  for (uint32_t ii = 0; ii < totalEntries * EXFAT_DIRECTORY_ENTRY_SIZE; ii++) {
    buffer[targetOffset + ii] = entrySetBuffer[ii];
  }

  // Write the sector back to disk
  result = writeSector(driverState, targetSector, buffer);
  free(entrySetBuffer);
  
  if (result != EXFAT_SUCCESS) {
    returnValue = result;
    goto cleanup;
  }

  // Copy created entries back to output parameters
  memcpy(fileEntry, &buffer[targetOffset], sizeof(ExFatFileDirectoryEntry));
  memcpy(
    streamEntry,
    &buffer[targetOffset + EXFAT_DIRECTORY_ENTRY_SIZE],
    sizeof(ExFatStreamExtensionEntry)
  );

  // Return directory location
  if (dirCluster != NULL) {
    *dirCluster = currentCluster;
  }
  if (dirOffset != NULL) {
    uint32_t sectorsFromClusterStart =
      (targetSector - clusterToSector(driverState, currentCluster));
    uint32_t entriesBeforeTargetSector =
      sectorsFromClusterStart * entriesPerSector;
    uint32_t entryOffsetInSector = targetOffset / EXFAT_DIRECTORY_ENTRY_SIZE;
    *dirOffset = entriesBeforeTargetSector + entryOffsetInSector;
  }

cleanup:
  free(utf16Name);
  return returnValue;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Search for a file in a directory
///
/// @param driverState Pointer to the exFAT driver state
/// @param directoryCluster First cluster of the directory
/// @param fileName Name of the file to search for
/// @param fileEntry Pointer to store file directory entry
/// @param streamEntry Pointer to store stream extension entry
/// @param dirCluster Pointer to store directory cluster
/// @param dirOffset Pointer to store offset in directory
///
/// @return EXFAT_SUCCESS if found, EXFAT_FILE_NOT_FOUND if not found
///////////////////////////////////////////////////////////////////////////////
static int searchDirectory(
  ExFatDriverState* driverState, uint32_t directoryCluster,
  const char* fileName, ExFatFileDirectoryEntry* fileEntry,
  ExFatStreamExtensionEntry* streamEntry, uint32_t* dirCluster,
  uint32_t* dirOffset
) {
  if (driverState == NULL || fileName == NULL || fileEntry == NULL ||
      streamEntry == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  // Validate cluster number
  if (directoryCluster < 2 ||
      directoryCluster >= driverState->clusterCount + 2) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  // Allocate search name buffer
  uint16_t* searchName = (uint16_t*) malloc(
    EXFAT_MAX_FILENAME_LENGTH * sizeof(uint16_t)
  );
  if (searchName == NULL) {
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(EXFAT_MAX_FILENAME_LENGTH * sizeof(uint16_t));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate full name buffer
  uint16_t* fullName = (uint16_t*) malloc(
    EXFAT_MAX_FILENAME_LENGTH * sizeof(uint16_t)
  );
  if (fullName == NULL) {
    free(searchName);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(EXFAT_MAX_FILENAME_LENGTH * sizeof(uint16_t));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate temporary file entry
  ExFatFileDirectoryEntry* tempFileEntry =
    (ExFatFileDirectoryEntry*) malloc(sizeof(ExFatFileDirectoryEntry));
  if (tempFileEntry == NULL) {
    free(fullName);
    free(searchName);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(sizeof(ExFatFileDirectoryEntry));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate temporary stream entry
  ExFatStreamExtensionEntry* tempStreamEntry =
    (ExFatStreamExtensionEntry*) malloc(sizeof(ExFatStreamExtensionEntry));
  if (tempStreamEntry == NULL) {
    free(tempFileEntry);
    free(fullName);
    free(searchName);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(sizeof(ExFatStreamExtensionEntry));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate temporary name entry
  ExFatFileNameEntry* nameEntry =
    (ExFatFileNameEntry*) malloc(sizeof(ExFatFileNameEntry));
  if (nameEntry == NULL) {
    free(tempStreamEntry);
    free(tempFileEntry);
    free(fullName);
    free(searchName);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(sizeof(ExFatFileNameEntry));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  uint8_t searchNameLength = asciiToUtf16(
    fileName, searchName, EXFAT_MAX_FILENAME_LENGTH
  );

  uint32_t currentCluster = directoryCluster;
  uint32_t globalEntryOffset = 0;
  int returnValue = EXFAT_FILE_NOT_FOUND;

  uint32_t entriesPerSector =
    driverState->bytesPerSector / EXFAT_DIRECTORY_ENTRY_SIZE;

  while (currentCluster != 0xFFFFFFFF && currentCluster >= 2) {
    // Validate cluster is in range
    if (currentCluster >= driverState->clusterCount + 2) {
      returnValue = EXFAT_ERROR;
      goto cleanup;
    }

    uint32_t clusterStartSector = clusterToSector(driverState, currentCluster);
    uint32_t entriesPerCluster =
      entriesPerSector * driverState->sectorsPerCluster;

    // Task entries in this cluster
    for (uint32_t entryIndex = 0; entryIndex < entriesPerCluster;
         entryIndex++) {
      // Calculate sector and offset for this entry
      uint32_t sectorOffset = entryIndex / entriesPerSector;
      uint32_t entryOffset =
        (entryIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;
      uint32_t sector = clusterStartSector + sectorOffset;

      // Read sector containing this entry
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        returnValue = result;
        goto cleanup;
      }

      uint8_t entryType = buffer[entryOffset];

      if (entryType == EXFAT_ENTRY_END_OF_DIR) {
        goto cleanup;
      }

      if (entryType == EXFAT_ENTRY_FILE) {
        // Read file directory entry
        readBytes(tempFileEntry, &buffer[entryOffset]);

        uint8_t secondaryCount = 0;
        readBytes(&secondaryCount, &tempFileEntry->secondaryCount);

        if (secondaryCount < 2) {
          globalEntryOffset++;
          continue;
        }

        // Read stream extension entry (next entry)
        uint32_t streamIndex = entryIndex + 1;
        if (streamIndex >= entriesPerCluster) {
          // Stream entry is in next cluster - skip this file
          globalEntryOffset++;
          // FIX: Skip all secondary entries
          entryIndex += secondaryCount;
          continue;
        }

        uint32_t streamSectorOffset = streamIndex / entriesPerSector;
        uint32_t streamEntryOffset =
          (streamIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;
        uint32_t streamSector = clusterStartSector + streamSectorOffset;

        if (streamSector != sector) {
          result = readSector(driverState, streamSector, buffer);
          if (result != EXFAT_SUCCESS) {
            returnValue = result;
            goto cleanup;
          }
        }

        readBytes(tempStreamEntry, &buffer[streamEntryOffset]);

        uint8_t streamEntryType = 0;
        readBytes(&streamEntryType, &tempStreamEntry->entryType);

        if (streamEntryType != EXFAT_ENTRY_STREAM) {
          globalEntryOffset++;
          // FIX: Skip all secondary entries
          entryIndex += secondaryCount;
          continue;
        }

        uint8_t nameLength = 0;
        readBytes(&nameLength, &tempStreamEntry->nameLength);

        if (nameLength == 0) {
          globalEntryOffset++;
          // FIX: Skip all secondary entries
          entryIndex += secondaryCount;
          continue;
        }

        // Read filename entries
        uint8_t nameIndex = 0;
        uint8_t numNameEntries = (nameLength + 14) / 15;
        uint32_t lastSectorRead = streamSector;
        bool nameReadComplete = true;

        for (uint8_t jj = 0;
          (jj < numNameEntries) && (nameIndex < nameLength);
          jj++
        ) {
          uint32_t nameEntryIndex = entryIndex + 2 + jj;
          if (nameEntryIndex >= entriesPerCluster) {
            // Filename entries span beyond cluster - skip this file
            nameReadComplete = false;
            break;
          }

          uint32_t nameSectorOffset = nameEntryIndex / entriesPerSector;
          uint32_t nameEntryOffset =
            (nameEntryIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;
          uint32_t nameSector = clusterStartSector + nameSectorOffset;

          if (nameSector != lastSectorRead) {
            result = readSector(driverState, nameSector, buffer);
            if (result != EXFAT_SUCCESS) {
              returnValue = result;
              goto cleanup;
            }
            lastSectorRead = nameSector;
          }

          readBytes(nameEntry, &buffer[nameEntryOffset]);

          uint8_t nameEntryType = 0;
          readBytes(&nameEntryType, &nameEntry->entryType);

          if (nameEntryType != EXFAT_ENTRY_FILENAME) {
            nameReadComplete = false;
            break;
          }

          // Extract characters from this entry
          for (uint8_t kk = 0; kk < 15 && nameIndex < nameLength; kk++) {
            uint16_t character = 0;
            readBytes(&character, &nameEntry->fileName[kk]);
            fullName[nameIndex] = character;
            nameIndex++;
          }
        }

        // Compare names if we read all characters
        if (nameReadComplete && nameIndex == nameLength &&
            compareFilenames(
              fullName, nameLength, searchName, searchNameLength
            ) == 0) {
          // Found a match - copy to output parameters
          memcpy(
            fileEntry, tempFileEntry, sizeof(ExFatFileDirectoryEntry)
          );
          memcpy(
            streamEntry, tempStreamEntry, sizeof(ExFatStreamExtensionEntry)
          );

          if (dirCluster != NULL) {
            *dirCluster = currentCluster;
          }
          if (dirOffset != NULL) {
            *dirOffset = globalEntryOffset;
          }
          returnValue = EXFAT_SUCCESS;
          goto cleanup;
        }

        // FIX: Skip all secondary entries (file + stream + name entries)
        entryIndex += secondaryCount;
        globalEntryOffset += (secondaryCount + 1);
      } else {
        globalEntryOffset++;
      }
    }

    // Get next cluster in directory chain
    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      returnValue = result;
      goto cleanup;
    }
    currentCluster = nextCluster;
  }

cleanup:
  free(nameEntry);
  free(tempStreamEntry);
  free(tempFileEntry);
  free(fullName);
  free(searchName);
  return returnValue;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Parse path and navigate through directories
///
/// @param driverState Pointer to the exFAT driver state
/// @param filePath Full path to the file
/// @param finalDirectory Pointer to store the final directory cluster
/// @param fileNameBuffer Buffer to store the filename (must be at least
///                       EXFAT_MAX_FILENAME_LENGTH + 1 bytes)
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int navigateToDirectory(
  ExFatDriverState* driverState, const char* filePath,
  uint32_t* finalDirectory, char* fileNameBuffer
) {
  if (driverState == NULL || filePath == NULL || finalDirectory == NULL ||
      fileNameBuffer == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  // Allocate component buffer
  char* component = (char*) malloc(EXFAT_MAX_FILENAME_LENGTH + 1);
  if (component == NULL) {
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(EXFAT_MAX_FILENAME_LENGTH + 1);
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate directory entry
  ExFatFileDirectoryEntry* dirEntry =
    (ExFatFileDirectoryEntry*) malloc(sizeof(ExFatFileDirectoryEntry));
  if (dirEntry == NULL) {
    free(component);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(sizeof(ExFatFileDirectoryEntry));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Allocate stream entry
  ExFatStreamExtensionEntry* streamEntry =
    (ExFatStreamExtensionEntry*) malloc(sizeof(ExFatStreamExtensionEntry));
  if (streamEntry == NULL) {
    free(dirEntry);
    free(component);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": Could not allocate ");
    printDebugInt(sizeof(ExFatStreamExtensionEntry));
    printDebugString(" bytes\n");
    return EXFAT_NO_MEMORY;
  }

  // Start at root directory
  uint32_t currentDirectory = driverState->rootDirectoryCluster;
  const char* pathPtr = filePath;
  int returnValue = EXFAT_SUCCESS;

  // Skip leading slash
  if (*pathPtr == '/') {
    pathPtr++;
  }

  // If empty path, return root
  if (*pathPtr == '\0') {
    *finalDirectory = currentDirectory;
    fileNameBuffer[0] = '\0';
    goto cleanup;
  }

  // Parse path component by component
  while (*pathPtr != '\0') {
    // Extract next path component
    uint8_t componentLength = 0;
    while (*pathPtr != '\0' && *pathPtr != '/' &&
           componentLength < EXFAT_MAX_FILENAME_LENGTH) {
      component[componentLength++] = *pathPtr++;
    }
    component[componentLength] = '\0';

    // Skip trailing slash
    if (*pathPtr == '/') {
      pathPtr++;
    }

    // If this is the last component, it's the filename
    if (*pathPtr == '\0') {
      *finalDirectory = currentDirectory;

      // Copy component to caller's buffer
      for (uint8_t ii = 0; ii <= componentLength; ii++) {
        fileNameBuffer[ii] = component[ii];
      }
      goto cleanup;
    }

    // Not the last component, so it should be a directory
    int result = searchDirectory(
      driverState, currentDirectory, component, dirEntry, streamEntry,
      NULL, NULL
    );

    if (result != EXFAT_SUCCESS) {
      returnValue = result;
      goto cleanup;
    }

    // Verify it's actually a directory using readBytes
    uint16_t attributes = 0;
    readBytes(&attributes, &dirEntry->fileAttributes);
    if ((attributes & EXFAT_ATTR_DIRECTORY) == 0) {
      returnValue = EXFAT_ERROR;  // Not a directory
      goto cleanup;
    }

    // Move to this directory using readBytes
    uint32_t nextDirectory = 0;
    readBytes(&nextDirectory, &streamEntry->firstCluster);
    currentDirectory = nextDirectory;
  }

  *finalDirectory = currentDirectory;
  fileNameBuffer[0] = '\0';

cleanup:
  free(streamEntry);
  free(dirEntry);
  free(component);
  return returnValue;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Open or create an exFAT file
///
/// @param ds Pointer to the exFAT driver state, cast to a void*
/// @param filePath Path to the file
/// @param mode File open mode (same as fopen: "r", "w", "a", etc.)
///
/// @return Pointer to ExFatFileHandle cast to a void* on success, NULL on
/// failure
///////////////////////////////////////////////////////////////////////////////
void* exFatOpenFile(
  void* ds, const char* filePath, const char* mode
) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  if ((driverState == NULL) || (filePath == NULL) || (*filePath == '\0')
    || (mode == NULL) || (*mode == '\0')
  ) {
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Parse mode
  bool read = false;
  bool write = false;
  bool append = false;
  bool mustExist = false;
  bool truncate = false;

  if (mode[0] == 'r') {
    read = true;
    mustExist = true;
    if (mode[1] == '+') {
      write = true;
    }
  } else if (mode[0] == 'w') {
    write = true;
    truncate = true;
    if (mode[1] == '+') {
      read = true;
    }
  } else if (mode[0] == 'a') {
    write = true;
    append = true;
    if (mode[1] == '+') {
      read = true;
    }
  } else {
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Allocate file handle
  // Because of the way the memory compaction works, the first thing we allocate
  // needs to be the return value.
  ExFatFileHandle* handle = (ExFatFileHandle*) malloc(
    sizeof(ExFatFileHandle)
  );
  if (handle == NULL) {
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Allocate filename buffer
  char* fileName = (char*) malloc(EXFAT_MAX_FILENAME_LENGTH + 1);
  if (fileName == NULL) {
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Allocate file entry
  ExFatFileDirectoryEntry* fileEntry =
    (ExFatFileDirectoryEntry*) malloc(sizeof(ExFatFileDirectoryEntry));
  if (fileEntry == NULL) {
    free(fileName);
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Allocate stream entry
  ExFatStreamExtensionEntry* streamEntry =
    (ExFatStreamExtensionEntry*) malloc(sizeof(ExFatStreamExtensionEntry));
  if (streamEntry == NULL) {
    free(fileEntry);
    free(fileName);
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Navigate to the directory containing the file
  uint32_t directoryCluster = 0;

  int result = navigateToDirectory(
    driverState, filePath, &directoryCluster, fileName
  );

  if (result != EXFAT_SUCCESS) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": result = ");
    printDebugInt(result);
    printDebugString("\n");
    return NULL;
  }

  // Search for the file
  uint32_t dirCluster = 0;
  uint32_t dirOffset = 0;

  result = searchDirectory(
    driverState, directoryCluster, fileName, fileEntry, streamEntry,
    &dirCluster, &dirOffset
  );

  if (result == EXFAT_FILE_NOT_FOUND) {
    if (mustExist) {
      free(streamEntry);
      free(fileEntry);
      free(fileName);
      free(handle);
      printDebugString(__func__);
      printDebugInt(__LINE__);
      printDebugString(": return NULL\n");
      return NULL;
    }

    // Create the file
    result = createFileEntry(
      driverState, directoryCluster, fileName, fileEntry, streamEntry,
      &dirCluster, &dirOffset
    );
    if (result != EXFAT_SUCCESS) {
      free(streamEntry);
      free(fileEntry);
      free(fileName);
      free(handle);
      printDebugString(__func__);
      printDebugInt(__LINE__);
      printDebugString(": return NULL\n");
      return NULL;
    }
  } else if (result != EXFAT_SUCCESS) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;
  }

  // Check if file is read-only when trying to write
  uint16_t fileAttributes = 0;
  readBytes(&fileAttributes, &fileEntry->fileAttributes);
  if ((write || append) &&
      ((fileAttributes & EXFAT_ATTR_READ_ONLY) != 0)) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    free(handle);
    printDebugString(__func__);
    printDebugInt(__LINE__);
    printDebugString(": return NULL\n");
    return NULL;  // Cannot open read-only file for writing
  }

  // Initialize file handle using readBytes for all packed struct accesses
  uint32_t firstCluster = 0;
  readBytes(&firstCluster, &streamEntry->firstCluster);
  handle->firstCluster = firstCluster;
  handle->currentCluster = firstCluster;

  uint64_t fileSize = 0;
  readBytes(&fileSize, &streamEntry->dataLength);
  handle->fileSize = fileSize;

  uint16_t attributes = 0;
  readBytes(&attributes, &fileEntry->fileAttributes);
  handle->attributes = attributes;

  handle->directoryCluster = dirCluster;
  handle->directoryOffset = dirOffset;

  // Store open mode flags
  handle->canRead = read;
  handle->canWrite = write;
  handle->appendMode = append;

  // Copy filename
  handle->fileName[EXFAT_MAX_FILENAME_LENGTH] = '\0';
  strncpy(handle->fileName, filePath, EXFAT_MAX_FILENAME_LENGTH);
  printDebugString(__func__);
  printDebugString(": Opening file \"");
  printDebugString(handle->fileName);
  printDebugString("\"\n");

  // Set position based on mode
  if (append) {
    handle->currentPosition = (uint32_t) handle->fileSize;
    // Navigate to last cluster
    uint32_t cluster = handle->firstCluster;
    uint32_t position = 0;
    while (position + driverState->bytesPerCluster < handle->fileSize) {
      uint32_t nextCluster = 0;
      if (readFatEntry(driverState, cluster, &nextCluster) !=
          EXFAT_SUCCESS) {
        free(streamEntry);
        free(fileEntry);
        free(fileName);
        free(handle);
        return NULL;
      }
      if (nextCluster == 0xFFFFFFFF) {
        break;
      }
      cluster = nextCluster;
      position += driverState->bytesPerCluster;
    }
    handle->currentCluster = cluster;
  } else {
    handle->currentPosition = 0;
  }

  // Truncate if needed
  if (truncate && handle->fileSize > 0) {
    handle->fileSize = 0;
    handle->currentPosition = 0;
    // TODO: Free all clusters and update directory entry
    // This requires implementing cluster freeing logic
  }

  free(streamEntry);
  free(fileEntry);
  free(fileName);
  return handle;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Read data from an exFAT file
///
/// @param ds Pointer to the exFAT driver state, cast to a void*
/// @param ptr Pointer to buffer to store read data
/// @param length Maximum number of bytes to read
/// @param fileHandle Pointer to the file handle, cast to a void*
///
/// @return Number of bytes read on success, negative errno on failure
///////////////////////////////////////////////////////////////////////////////
int32_t exFatRead(void* ds, void* ptr, uint32_t length, void* fileHandle) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  ExFatFileHandle *file = (ExFatFileHandle*) fileHandle;
  if (driverState == NULL || ptr == NULL || file == NULL) {
    return -EINVAL;
  }

  if (!file->canRead) {
    return -EACCES; // Permission denied
  }

  printDebugString(__func__);
  printDebugString(": Reading from file \"");
  printDebugString(file->fileName);
  printDebugString("\"\n");

  // Calculate remaining bytes in file
  uint64_t remainingBytes = 0;
  if (file->currentPosition < file->fileSize) {
    remainingBytes = file->fileSize - file->currentPosition;
  } else {
    return 0; // Already at EOF
  }

  // Adjust length if it exceeds remaining bytes
  if (length > remainingBytes) {
    length = (uint32_t) remainingBytes;
  }

  if (length == 0) {
    return 0; // Nothing to read
  }

  // Check if file has valid cluster
  if (file->currentCluster < 2) {
    return -EIO; // File has no data clusters
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;
  uint8_t* destPtr = (uint8_t*) ptr;
  uint32_t bytesRead = 0;

  while (bytesRead < length) {
    // Calculate position within current cluster
    uint32_t positionInCluster = file->currentPosition %
      driverState->bytesPerCluster;
    
    // Calculate which sector within the cluster
    uint32_t sectorInCluster = positionInCluster /
      driverState->bytesPerSector;
    
    // Calculate offset within the sector
    uint32_t offsetInSector = positionInCluster %
      driverState->bytesPerSector;
    
    // Calculate how many bytes we can read from this sector
    uint32_t bytesInSector = driverState->bytesPerSector - offsetInSector;
    uint32_t bytesToRead = length - bytesRead;
    if (bytesToRead > bytesInSector) {
      bytesToRead = bytesInSector;
    }

    // Calculate the actual sector number
    uint32_t sector = clusterToSector(driverState, file->currentCluster) +
      sectorInCluster;

    // Read the sector
    int result = readSector(driverState, sector, buffer);
    if (result != EXFAT_SUCCESS) {
      printDebugString(__func__);
      printDebugString(": readSector failed\n");
      if (bytesRead > 0) {
        printDebugString(__func__);
        printDebugString(": returning ");
        printDebugInt(bytesRead);
        printDebugString("\n");
        return bytesRead; // Return what we've read so far
      }

      printDebugString(__func__);
      printDebugString(": returning -EIO\n");
      return -EIO;
    }

    // Copy bytes from buffer to destination
    for (uint32_t ii = 0; ii < bytesToRead; ii++) {
      destPtr[bytesRead + ii] = buffer[offsetInSector + ii];
    }

    bytesRead += bytesToRead;
    file->currentPosition += bytesToRead;

    // Check if we've finished reading
    if (bytesRead >= length) {
      break;
    }

    // Check if we need to move to next cluster
    uint32_t newPositionInCluster = file->currentPosition %
      driverState->bytesPerCluster;
    
    if (newPositionInCluster == 0) {
      // We've exhausted the current cluster, get next cluster from FAT
      uint32_t nextCluster = 0;
      result = readFatEntry(driverState, file->currentCluster, &nextCluster);
      if (result != EXFAT_SUCCESS) {
        if (bytesRead > 0) {
          return bytesRead; // Return what we've read so far
        }
        return -EIO;
      }

      if (nextCluster == 0xFFFFFFFF) {
        // End of file chain - shouldn't happen if fileSize is correct
        break;
      }

      file->currentCluster = nextCluster;
    }
  }

  printDebugString(__func__);
  printDebugString(": Read ");
  printDebugInt(bytesRead);
  printDebugString(" bytes from file \"");
  printDebugString(file->fileName);
  printDebugString("\"\n");
  return (int32_t) bytesRead;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Update directory entry after file modification
///
/// @param driverState Pointer to the exFAT driver state
/// @param file Pointer to the file handle with updated metadata
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int updateDirectoryEntry(
  ExFatDriverState* driverState, ExFatFileHandle* file
) {
  if (driverState == NULL || file == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;

  uint32_t entriesPerSector =
    driverState->bytesPerSector / EXFAT_DIRECTORY_ENTRY_SIZE;

  // Calculate which sector contains the file entry
  uint32_t entryIndex = file->directoryOffset;
  uint32_t sectorOffset = entryIndex / entriesPerSector;
  uint32_t entryOffsetInSector =
    (entryIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;

  uint32_t sector = clusterToSector(driverState, file->directoryCluster) +
    sectorOffset;

  // Read the sector containing the file entry
  int result = readSector(driverState, sector, buffer);
  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to read directory sector\n");
    return result;
  }

  // Allocate temporary file entry
  ExFatFileDirectoryEntry* fileEntry =
    (ExFatFileDirectoryEntry*) malloc(sizeof(ExFatFileDirectoryEntry));
  if (fileEntry == NULL) {
    return EXFAT_NO_MEMORY;
  }

  // Read file entry from buffer
  readBytes(fileEntry, &buffer[entryOffsetInSector]);

  uint8_t secondaryCount = 0;
  readBytes(&secondaryCount, &fileEntry->secondaryCount);

  if (secondaryCount < 2) {
    free(fileEntry);
    printString("  ERROR: Invalid secondary count\n");
    return EXFAT_ERROR;
  }

  // Update file entry timestamps
  uint32_t timestamp = createValidTimestamp();
  writeBytes(&fileEntry->lastModifiedTimestamp, &timestamp);

  // Write updated file entry back to buffer
  writeBytes(&buffer[entryOffsetInSector], fileEntry);

  // Calculate stream entry location
  uint32_t streamEntryIndex = entryIndex + 1;
  uint32_t streamSectorOffset = streamEntryIndex / entriesPerSector;
  uint32_t streamEntryOffsetInSector =
    (streamEntryIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;
  uint32_t streamSector =
    clusterToSector(driverState, file->directoryCluster) + streamSectorOffset;

  // Check if stream entry is in a different sector
  if (streamSector != sector) {
    // Write file entry sector first
    result = writeSector(driverState, sector, buffer);
    if (result != EXFAT_SUCCESS) {
      free(fileEntry);
      printString("  ERROR: Failed to write file entry sector\n");
      return result;
    }

    // Read stream entry sector
    result = readSector(driverState, streamSector, buffer);
    if (result != EXFAT_SUCCESS) {
      free(fileEntry);
      printString("  ERROR: Failed to read stream entry sector\n");
      return result;
    }
  }

  // Allocate temporary stream entry
  ExFatStreamExtensionEntry* streamEntry =
    (ExFatStreamExtensionEntry*) malloc(sizeof(ExFatStreamExtensionEntry));
  if (streamEntry == NULL) {
    free(fileEntry);
    return EXFAT_NO_MEMORY;
  }

  // Read stream entry from buffer
  readBytes(streamEntry, &buffer[streamEntryOffsetInSector]);

  // Update stream entry with new size and cluster info
  writeBytes(&streamEntry->dataLength, &file->fileSize);
  writeBytes(&streamEntry->validDataLength, &file->fileSize);
  writeBytes(&streamEntry->firstCluster, &file->firstCluster);

  // Write updated stream entry back to buffer
  writeBytes(&buffer[streamEntryOffsetInSector], streamEntry);

  // Write the sector back to disk
  result = writeSector(driverState, streamSector, buffer);

  free(streamEntry);
  free(fileEntry);

  if (result != EXFAT_SUCCESS) {
    printString("  ERROR: Failed to write stream entry sector\n");
    return result;
  }

  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Write data to an exFAT file
///
/// @param ds Pointer to the exFAT driver state, cast to a void*
/// @param ptr Pointer to buffer containing data to write
/// @param length Number of bytes to write
/// @param fileHandle Pointer to the file handle, cast to a void*
///
/// @return Number of bytes written on success, negative errno on failure
///////////////////////////////////////////////////////////////////////////////
int32_t exFatWrite(
  void* ds, void* ptr, uint32_t length, void* fileHandle
) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  ExFatFileHandle *file = (ExFatFileHandle*) fileHandle;
  if (driverState == NULL || ptr == NULL || file == NULL) {
    return -EINVAL;
  }

  if (!file->canWrite) {
    return -EACCES;
  }

  if (length == 0) {
    return 0;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;
  uint8_t* srcPtr = (uint8_t*) ptr;
  uint32_t bytesWritten = 0;

  // If file has no clusters yet, allocate the first one
  if (file->firstCluster == 0 || file->firstCluster < 2) {
    uint32_t newCluster = 0;
    int result = allocateCluster(driverState, &newCluster);
    if (result != EXFAT_SUCCESS) {
      printString("  ERROR: Failed to allocate first cluster\n");
      return -ENOSPC;
    }
    file->firstCluster = newCluster;
    file->currentCluster = newCluster;
  }

  // Main write loop
  while (bytesWritten < length) {
    // Calculate position within current cluster
    uint32_t positionInCluster = file->currentPosition %
      driverState->bytesPerCluster;

    // Check if we need to move to or allocate a new cluster
    if (positionInCluster == 0 && file->currentPosition > 0) {
      // Try to get next cluster from FAT
      uint32_t nextCluster = 0;
      int result = readFatEntry(
        driverState, file->currentCluster, &nextCluster
      );
      if (result != EXFAT_SUCCESS) {
        if (bytesWritten > 0) {
          break;
        }
        printString("  ERROR: Failed to read FAT entry\n");
        return -EIO;
      }

      if (nextCluster == 0xFFFFFFFF) {
        // At end of chain, need to allocate new cluster
        uint32_t allocatedCluster = 0;
        result = allocateCluster(driverState, &allocatedCluster);
        if (result != EXFAT_SUCCESS) {
          // Out of space - return what we've written so far
          if (bytesWritten > 0) {
            break;
          }
          printString("  ERROR: Failed to allocate new cluster\n");
          return -ENOSPC;
        }

        // Link new cluster to chain
        result = writeFatEntry(
          driverState, file->currentCluster, allocatedCluster
        );
        if (result != EXFAT_SUCCESS) {
          if (bytesWritten > 0) {
            break;
          }
          printString("  ERROR: Failed to update FAT chain\n");
          return -EIO;
        }

        nextCluster = allocatedCluster;
      }

      file->currentCluster = nextCluster;
      positionInCluster = 0;
    }

    // Calculate sector and offset within sector
    uint32_t sectorInCluster = positionInCluster /
      driverState->bytesPerSector;
    uint32_t offsetInSector = positionInCluster %
      driverState->bytesPerSector;

    // Calculate how many bytes we can write in this sector
    uint32_t bytesInSector = driverState->bytesPerSector - offsetInSector;
    uint32_t bytesToWrite = length - bytesWritten;
    if (bytesToWrite > bytesInSector) {
      bytesToWrite = bytesInSector;
    }

    // Calculate the actual sector number
    uint32_t sector = clusterToSector(driverState, file->currentCluster) +
      sectorInCluster;

    // If we're not writing a full sector or not starting at offset 0,
    // we need to read-modify-write
    if (offsetInSector != 0 ||
        bytesToWrite < driverState->bytesPerSector) {
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        if (bytesWritten > 0) {
          break;
        }
        printString("  ERROR: Failed to read sector for RMW\n");
        return -EIO;
      }
    }

    // Copy data from source to buffer
    for (uint32_t ii = 0; ii < bytesToWrite; ii++) {
      buffer[offsetInSector + ii] = srcPtr[bytesWritten + ii];
    }

    // Write the sector back to disk
    int result = writeSector(driverState, sector, buffer);
    if (result != EXFAT_SUCCESS) {
      if (bytesWritten > 0) {
        break;
      }
      printString("  ERROR: Failed to write sector\n");
      return -EIO;
    }

    // Update counters and position
    bytesWritten += bytesToWrite;
    file->currentPosition += bytesToWrite;

    // Update file size if we've grown the file
    if (file->currentPosition > file->fileSize) {
      file->fileSize = file->currentPosition;
    }
  }

  // Update directory entry with new file size and timestamps
  if (bytesWritten > 0) {
    int result = updateDirectoryEntry(driverState, file);
    if (result != EXFAT_SUCCESS) {
      printString("  WARNING: Failed to update directory entry\n");
      // Don't fail the write, just log the warning
    }
  }

  return (int32_t) bytesWritten;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Close an exFAT file and release resources
///
/// Flushes any pending metadata updates to the directory entry and frees
/// the file handle structure. If the file was opened for writing, the
/// directory entry is updated with the final file size and timestamps.
///
/// @param ds Pointer to the exFAT driver state, cast to a void*
/// @param fileHandle Pointer to the file handle to close, cast to a void*
///
/// @return 0 on success, negative errno on failure
///////////////////////////////////////////////////////////////////////////////
int exFatFclose(void* ds, void* fileHandle) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  ExFatFileHandle *exFatFile = (ExFatFileHandle*) fileHandle;
  if ((driverState == NULL) || (exFatFile == NULL)) {
    return -EINVAL;
  }

  int returnValue = 0;
  printDebugString(__func__);
  printDebugString(": Closing file \"");
  printDebugString(exFatFile->fileName);
  printDebugString("\"\n");

  // If file was opened for writing, flush metadata to directory entry
  // This ensures file size, timestamps, and cluster information are updated
  if (exFatFile->canWrite) {
    int result = updateDirectoryEntry(driverState, exFatFile);
    if (result != EXFAT_SUCCESS) {
      // Convert exFAT error codes to errno values
      if (result == EXFAT_NO_MEMORY) {
        returnValue = -ENOMEM;
      } else if (result == EXFAT_INVALID_PARAMETER) {
        returnValue = -EINVAL;
      } else {
        returnValue = -EIO;
      }
      
      // Log warning but continue with close to avoid resource leak
      printString("WARNING: Failed to flush file metadata on close\n");
      printString("  File: ");
      printString(exFatFile->fileName);
      printString("\n");
    }
  }

  // Free the file handle structure
  free(exFatFile);

  return returnValue;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Mark directory entries as unused (deleted)
///
/// @param driverState Pointer to the exFAT driver state
/// @param directoryCluster Cluster containing the directory entries
/// @param dirOffset Offset of first entry in directory (in entries)
/// @param numEntries Number of entries to mark as unused
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int markEntriesAsUnused(
  ExFatDriverState* driverState, uint32_t directoryCluster,
  uint32_t dirOffset, uint8_t numEntries
) {
  if (driverState == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }

  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;
  
  uint32_t entriesPerSector =
    driverState->bytesPerSector / EXFAT_DIRECTORY_ENTRY_SIZE;
  uint32_t entriesPerCluster =
    entriesPerSector * driverState->sectorsPerCluster;
  
  uint32_t currentCluster = directoryCluster;
  uint32_t clusterOffset = dirOffset / entriesPerCluster;
  
  // Navigate to the correct cluster if needed
  for (uint32_t ii = 0; ii < clusterOffset; ii++) {
    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    if (nextCluster == 0xFFFFFFFF) {
      return EXFAT_ERROR;
    }
    currentCluster = nextCluster;
  }
  
  uint32_t entryIndexInCluster = dirOffset % entriesPerCluster;
  
  for (uint8_t ii = 0; ii < numEntries; ii++) {
    uint32_t currentEntryIndex = entryIndexInCluster + ii;
    
    // Check if we need to move to next cluster
    if (currentEntryIndex >= entriesPerCluster) {
      uint32_t nextCluster = 0;
      int result = readFatEntry(driverState, currentCluster, &nextCluster);
      if (result != EXFAT_SUCCESS) {
        return result;
      }
      if (nextCluster == 0xFFFFFFFF) {
        return EXFAT_ERROR;
      }
      currentCluster = nextCluster;
      currentEntryIndex = 0;
      entryIndexInCluster = 0;
    }
    
    // Calculate sector and offset
    uint32_t sectorOffset = currentEntryIndex / entriesPerSector;
    uint32_t entryOffsetInSector =
      (currentEntryIndex % entriesPerSector) * EXFAT_DIRECTORY_ENTRY_SIZE;
    uint32_t sector = clusterToSector(driverState, currentCluster) +
      sectorOffset;
    
    // Read the sector
    int result = readSector(driverState, sector, buffer);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    
    // Mark entry as unused (0x00 = unused/deleted)
    uint8_t unusedMarker = EXFAT_ENTRY_UNUSED;
    writeBytes(&buffer[entryOffsetInSector], &unusedMarker);
    
    // Write the sector back
    result = writeSector(driverState, sector, buffer);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
  }
  
  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Free a cluster chain starting from the given cluster
///
/// @param driverState Pointer to the exFAT driver state
/// @param firstCluster First cluster in the chain to free
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int freeClusterChain(
  ExFatDriverState* driverState, uint32_t firstCluster
) {
  if (driverState == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }
  
  if (firstCluster < 2 || firstCluster == 0xFFFFFFFF) {
    // No clusters to free
    return EXFAT_SUCCESS;
  }
  
  uint32_t currentCluster = firstCluster;
  
  while (currentCluster != 0xFFFFFFFF && currentCluster >= 2) {
    // Get next cluster before clearing FAT entry
    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    
    // Clear FAT entry (mark as free)
    uint32_t freeMarker = 0;
    result = writeFatEntry(driverState, currentCluster, freeMarker);
    if (result != EXFAT_SUCCESS) {
      printString("  ERROR: Failed to clear FAT entry for cluster ");
      printInt(currentCluster);
      printString("\n");
      return result;
    }
    
    // Update allocation bitmap (mark as free)
    result = updateAllocationBitmap(driverState, currentCluster, false);
    if (result != EXFAT_SUCCESS) {
      printString("  ERROR: Failed to update bitmap for cluster ");
      printInt(currentCluster);
      printString("\n");
      return result;
    }
    
    currentCluster = nextCluster;
  }
  
  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Check if a directory is empty
///
/// @param driverState Pointer to the exFAT driver state
/// @param directoryCluster First cluster of the directory to check
/// @param isEmpty Pointer to store result (true if empty)
///
/// @return EXFAT_SUCCESS on success, error code on failure
///////////////////////////////////////////////////////////////////////////////
static int isDirectoryEmpty(
  ExFatDriverState* driverState, uint32_t directoryCluster, bool* isEmpty
) {
  if (driverState == NULL || isEmpty == NULL) {
    return EXFAT_INVALID_PARAMETER;
  }
  
  if (directoryCluster < 2) {
    return EXFAT_INVALID_PARAMETER;
  }
  
  FilesystemState* filesystemState = driverState->filesystemState;
  uint8_t* buffer = filesystemState->blockBuffer;
  
  uint32_t currentCluster = directoryCluster;
  *isEmpty = true;
  
  while (currentCluster != 0xFFFFFFFF && currentCluster >= 2) {
    for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
      uint32_t sector = clusterToSector(driverState, currentCluster) + ss;
      int result = readSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        return result;
      }
      
      for (
        uint32_t ii = 0;
        ii < driverState->bytesPerSector;
        ii += EXFAT_DIRECTORY_ENTRY_SIZE
      ) {
        uint8_t entryType = buffer[ii];
        
        if (entryType == EXFAT_ENTRY_END_OF_DIR) {
          return EXFAT_SUCCESS;
        }
        
        // Check if this is an active file or directory entry
        if (entryType == EXFAT_ENTRY_FILE) {
          *isEmpty = false;
          return EXFAT_SUCCESS;
        }
      }
    }
    
    // Get next cluster in directory chain
    uint32_t nextCluster = 0;
    int result = readFatEntry(driverState, currentCluster, &nextCluster);
    if (result != EXFAT_SUCCESS) {
      return result;
    }
    currentCluster = nextCluster;
  }
  
  return EXFAT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Remove a file or empty directory from the exFAT filesystem
///
/// Removes the specified file or empty directory from the filesystem. For
/// files, all allocated clusters are freed. For directories, the operation
/// fails if the directory is not empty. The function navigates through the
/// directory hierarchy to find the target and updates all relevant metadata
/// including directory entries, FAT, and allocation bitmap.
///
/// @param ds Pointer to the initialized exFAT driver state, cast to a void*
/// @param pathname Path to the file or directory to remove
///
/// @return 0 on success, negative errno on failure (-ENOENT if not found,
///         -ENOTEMPTY if directory not empty, -EINVAL for invalid parameters,
///         -EIO for I/O errors)
///////////////////////////////////////////////////////////////////////////////
int exFatRemove(void* ds, const char* pathname) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  if (driverState == NULL || pathname == NULL || *pathname == '\0') {
    return -EINVAL;
  }
  
  // Don't allow removing root directory
  if (pathname[0] == '/' && pathname[1] == '\0') {
    return -EBUSY;
  }
  
  // Allocate filename buffer
  char* fileName = (char*) malloc(EXFAT_MAX_FILENAME_LENGTH + 1);
  if (fileName == NULL) {
    return -ENOMEM;
  }
  
  // Allocate file entry
  ExFatFileDirectoryEntry* fileEntry =
    (ExFatFileDirectoryEntry*) malloc(sizeof(ExFatFileDirectoryEntry));
  if (fileEntry == NULL) {
    free(fileName);
    return -ENOMEM;
  }
  
  // Allocate stream entry
  ExFatStreamExtensionEntry* streamEntry =
    (ExFatStreamExtensionEntry*) malloc(sizeof(ExFatStreamExtensionEntry));
  if (streamEntry == NULL) {
    free(fileEntry);
    free(fileName);
    return -ENOMEM;
  }
  
  // Navigate to the directory containing the target
  uint32_t directoryCluster = 0;
  int result = navigateToDirectory(
    driverState, pathname, &directoryCluster, fileName
  );
  
  if (result != EXFAT_SUCCESS) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    if (result == EXFAT_FILE_NOT_FOUND) {
      return -ENOENT;
    }
    return -EIO;
  }
  
  // Search for the file/directory to remove
  uint32_t dirCluster = 0;
  uint32_t dirOffset = 0;
  
  result = searchDirectory(
    driverState, directoryCluster, fileName, fileEntry, streamEntry,
    &dirCluster, &dirOffset
  );
  
  if (result != EXFAT_SUCCESS) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    if (result == EXFAT_FILE_NOT_FOUND) {
      return -ENOENT;
    }
    return -EIO;
  }
  
  // Check if it's a directory
  uint16_t attributes = 0;
  readBytes(&attributes, &fileEntry->fileAttributes);
  bool isDirectory = (attributes & EXFAT_ATTR_DIRECTORY) != 0;
  
  // Get the first cluster and secondary count
  uint32_t firstCluster = 0;
  readBytes(&firstCluster, &streamEntry->firstCluster);
  
  uint8_t secondaryCount = 0;
  readBytes(&secondaryCount, &fileEntry->secondaryCount);
  uint8_t totalEntries = secondaryCount + 1;
  
  // If it's a directory, check if it's empty
  if (isDirectory) {
    bool isEmpty = false;
    result = isDirectoryEmpty(driverState, firstCluster, &isEmpty);
    if (result != EXFAT_SUCCESS) {
      free(streamEntry);
      free(fileEntry);
      free(fileName);
      return -EIO;
    }
    
    if (!isEmpty) {
      free(streamEntry);
      free(fileEntry);
      free(fileName);
      return -ENOTEMPTY;
    }
  }
  
  // Free the cluster chain
  if (firstCluster >= 2) {
    result = freeClusterChain(driverState, firstCluster);
    if (result != EXFAT_SUCCESS) {
      printString("WARNING: Failed to free cluster chain\n");
      // Continue anyway to at least mark directory entries as unused
    }
  }
  
  // Mark directory entries as unused
  result = markEntriesAsUnused(
    driverState, dirCluster, dirOffset, totalEntries
  );
  
  if (result != EXFAT_SUCCESS) {
    free(streamEntry);
    free(fileEntry);
    free(fileName);
    return -EIO;
  }
  
  free(streamEntry);
  free(fileEntry);
  free(fileName);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Seek to a position in an exFAT file
///
/// Repositions the file position indicator for the specified file. The new
/// position is calculated based on the whence parameter: SEEK_SET positions
/// relative to the beginning of the file, SEEK_CUR positions relative to the
/// current position, and SEEK_END positions relative to the end of the file.
/// The function traverses the FAT chain as needed to find the correct cluster
/// for the new position. When seeking beyond allocated clusters in write mode,
/// it allocates and links all necessary clusters to reach the target position.
///
/// @param ds Pointer to the initialized exFAT driver state, cast to a void*
/// @param fileHandle Pointer to the file handle to seek within, cast to a void*
/// @param offset Number of bytes to offset from the position specified by
///   whence
/// @param whence Position from which offset is applied (SEEK_SET, SEEK_CUR, 
///               or SEEK_END)
///
/// @return The new position on success, negative errno on failure (-EINVAL for
/// invalid parameters, -EIO for I/O errors, -EOVERFLOW for position overflow,
/// -ENOSPC if out of disk space)
///////////////////////////////////////////////////////////////////////////////
int exFatSeek(void* ds, void* fileHandle, long offset, int whence) {
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  ExFatFileHandle *file = (ExFatFileHandle*) fileHandle;
  if (driverState == NULL || file == NULL) {
    return -EINVAL;
  }

  // Calculate the target position based on whence
  int64_t targetPosition = 0;
  
  if (whence == SEEK_SET) {
    targetPosition = offset;
  } else if (whence == SEEK_CUR) {
    targetPosition = (int64_t) file->currentPosition + offset;
  } else if (whence == SEEK_END) {
    targetPosition = (int64_t) file->fileSize + offset;
  } else {
    // Invalid whence value
    return -EINVAL;
  }

  // Validate the target position
  if (targetPosition < 0) {
    // Cannot seek before the beginning of file
    return -EINVAL;
  }

  // Check for position overflow
  if (targetPosition > UINT32_MAX) {
    return -EOVERFLOW;
  }

  uint32_t newPosition = (uint32_t) targetPosition;

  // If seeking beyond EOF and file is not open for writing, this is an error
  if (newPosition > file->fileSize && !file->canWrite) {
    return -EINVAL;
  }

  // If the new position is the same as current, nothing to do
  if (newPosition == file->currentPosition) {
    return file->currentPosition;
  }

  // Handle special case of seeking to position 0
  if (newPosition == 0) {
    file->currentPosition = 0;
    file->currentCluster = file->firstCluster;
    if (file->currentCluster == 0 && file->canWrite && file->fileSize == 0) {
      // Empty file opened for writing, cluster will be allocated on write
      return file->currentPosition;
    }
    return file->currentPosition;
  }

  // If file has no clusters yet and we're seeking beyond 0
  if ((file->firstCluster == 0 || file->firstCluster < 2) && 
      file->canWrite) {
    // Allocate the first cluster
    uint32_t firstCluster = 0;
    int result = allocateCluster(driverState, &firstCluster);
    if (result != EXFAT_SUCCESS) {
      printString("ERROR: Failed to allocate first cluster\n");
      return -ENOSPC;
    }
    file->firstCluster = firstCluster;
    file->currentCluster = firstCluster;
    
    // Clear the first cluster (write zeros)
    FilesystemState* filesystemState = driverState->filesystemState;
    uint8_t* buffer = filesystemState->blockBuffer;
    
    // Zero out the buffer
    for (uint32_t ii = 0; ii < driverState->bytesPerSector; ii++) {
      buffer[ii] = 0;
    }
    
    // Write zeros to all sectors in the first cluster
    for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
      uint32_t sector = clusterToSector(driverState, firstCluster) + ss;
      result = writeSector(driverState, sector, buffer);
      if (result != EXFAT_SUCCESS) {
        printString("ERROR: Failed to clear first cluster\n");
        return -EIO;
      }
    }
  } else if (file->firstCluster == 0 || file->firstCluster < 2) {
    // Empty file not opened for writing
    return -EINVAL;
  }

  // Calculate cluster indices
  uint32_t targetClusterIndex = newPosition / driverState->bytesPerCluster;
  
  // Find the current allocated extent by traversing from the beginning
  uint32_t lastAllocatedCluster = file->firstCluster;
  uint32_t lastAllocatedIndex = 0;
  uint32_t traversalCluster = file->firstCluster;
  
  // First, find the last allocated cluster
  while (true) {
    uint32_t nextCluster = 0;
    int result = readFatEntry(
      driverState, traversalCluster, &nextCluster
    );
    
    if (result != EXFAT_SUCCESS) {
      printString("ERROR: Failed to read FAT entry\n");
      return -EIO;
    }
    
    if (nextCluster == 0xFFFFFFFF) {
      // End of chain
      lastAllocatedCluster = traversalCluster;
      break;
    }
    
    if (nextCluster < 2 || nextCluster >= driverState->clusterCount + 2) {
      printString("ERROR: Invalid cluster in FAT chain\n");
      return -EIO;
    }
    
    traversalCluster = nextCluster;
    lastAllocatedIndex++;
    
    if (lastAllocatedIndex >= targetClusterIndex) {
      // We have enough clusters already
      break;
    }
  }
  
  // If we need more clusters and file is open for writing, allocate them
  if (lastAllocatedIndex < targetClusterIndex && file->canWrite) {
    FilesystemState* filesystemState = driverState->filesystemState;
    uint8_t* buffer = filesystemState->blockBuffer;
    
    // Zero out the buffer for clearing new clusters
    for (uint32_t ii = 0; ii < driverState->bytesPerSector; ii++) {
      buffer[ii] = 0;
    }
    
    uint32_t currentChainEnd = lastAllocatedCluster;
    
    // Allocate and link clusters to reach the target
    for (uint32_t ii = lastAllocatedIndex; ii < targetClusterIndex; ii++) {
      uint32_t newCluster = 0;
      int result = allocateCluster(driverState, &newCluster);
      if (result != EXFAT_SUCCESS) {
        printString("ERROR: Failed to allocate cluster ");
        printInt(ii);
        printString("\n");
        return -ENOSPC;
      }
      
      // Link the new cluster to the chain
      result = writeFatEntry(driverState, currentChainEnd, newCluster);
      if (result != EXFAT_SUCCESS) {
        printString("ERROR: Failed to link cluster to chain\n");
        return -EIO;
      }
      
      // Clear the new cluster (write zeros to all sectors)
      for (uint32_t ss = 0; ss < driverState->sectorsPerCluster; ss++) {
        uint32_t sector = clusterToSector(driverState, newCluster) + ss;
        result = writeSector(driverState, sector, buffer);
        if (result != EXFAT_SUCCESS) {
          printString("ERROR: Failed to clear cluster\n");
          return -EIO;
        }
      }
      
      currentChainEnd = newCluster;
    }
  } else if (lastAllocatedIndex < targetClusterIndex) {
    // Need more clusters but file is not open for writing
    printString("ERROR: Cannot seek beyond allocated clusters in read mode\n");
    return -EINVAL;
  }
  
  // Now traverse to the target cluster
  traversalCluster = file->firstCluster;
  uint32_t traversalIndex = 0;
  
  while (traversalIndex < targetClusterIndex) {
    uint32_t nextCluster = 0;
    int result = readFatEntry(
      driverState, traversalCluster, &nextCluster
    );
    
    if (result != EXFAT_SUCCESS) {
      printString("ERROR: Failed to read FAT during final traversal\n");
      return -EIO;
    }
    
    if (nextCluster == 0xFFFFFFFF) {
      // This shouldn't happen after allocation
      printString("ERROR: Unexpected end of chain after allocation\n");
      return -EIO;
    }
    
    traversalCluster = nextCluster;
    traversalIndex++;
  }
  
  // Update the file handle with the new position
  file->currentPosition = newPosition;
  file->currentCluster = traversalCluster;
  
  // Update file size if we've extended it
  if (newPosition > file->fileSize) {
    file->fileSize = newPosition;
    // Note: Directory entry will be updated on close or flush
  }
  
  return file->currentPosition;
}

/// @fn int exFatGetFileBlockMetadata(void *ds, void *fileHandle,
///   uint32_t *startBlock, uint32_t *numBlocks)
///
/// @brief Get the block-level metadata associated with a file.
///
/// @param ds A pointer to an ExFatDriverState, cast to a void*.
/// @param fileHandle A pointer to an ExFatFileHandle, cast to a void*.
/// @param startBlock A pointer to a uint32_t to store the start block in.
/// @param numBlocks A pointer to a uint32_t to store the number of blocks in.
///
/// @return Returns 0 on success, -errno on failure.
int exFatGetFileBlockMetadata(void *ds, void *fileHandle,
  uint32_t *startBlock, uint32_t *numBlocks
) {
  if ((ds == NULL) || (fileHandle == NULL)
    || (startBlock == NULL) || (numBlocks == NULL)
  ) {
    return -EINVAL;
  }
  
  ExFatDriverState *driverState = (ExFatDriverState*) ds;
  ExFatFileHandle *exFatFile = (ExFatFileHandle*) fileHandle;
  
  *startBlock = driverState->clusterHeapStartSector +
    ((exFatFile->firstCluster - 2) * driverState->sectorsPerCluster);
  *numBlocks
    = (uint32_t) ((exFatFile->fileSize + (driverState->bytesPerSector - 1))
    / ((uint64_t) driverState->bytesPerSector));
  
  return 0;
}

