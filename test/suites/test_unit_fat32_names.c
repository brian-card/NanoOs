///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.07.2026
///
/// @file              test_unit_fat32_names.c
///
/// @brief             Unit tests for how the FAT32 driver matches the names
///                    in a directory.
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

#include "NanoOsTest.h"

#include <stdlib.h>
#include <string.h>

#include "kernel/Filesystem.h"
#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/drivers/fat32/Fat32.h"

#define NAMES_SECTOR_SIZE 512
#define NAMES_NUM_SECTORS 8
#define NAMES_FAT_SECTOR  1
#define NAMES_DATA_SECTOR 2

static uint8_t namesDisk[NAMES_NUM_SECTORS][NAMES_SECTOR_SIZE];
static uint8_t namesBlockBuffer[NAMES_SECTOR_SIZE];
static BlockDevice namesBlockDevice;
static FilesystemState namesState;
static Fat32DriverState namesDriver;

/// @fn static int namesReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Read blocks from the in-memory disk.
///
/// @return Returns 0 on success, -1 if a block is past the end of the disk.
static int namesReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  (void) context;
  if (startBlock + numBlocks > NAMES_NUM_SECTORS) {
    return -1;
  }
  memcpy(buffer, namesDisk[startBlock], numBlocks * blockSize);
  return 0;
}

/// @fn static int namesWriteBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Write blocks to the in-memory disk.
///
/// @return Returns 0 on success, -1 if a block is past the end of the disk.
static int namesWriteBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  (void) context;
  if (startBlock + numBlocks > NAMES_NUM_SECTORS) {
    return -1;
  }
  memcpy(namesDisk[startBlock], buffer, numBlocks * blockSize);
  return 0;
}

/// @fn static void namesPutShortEntry(unsigned int slot,
///   const char *shortName, uint8_t ntReserved)
///
/// @brief Write a short directory entry with no long name into the root
/// directory, the way another system writes a name that fits 8.3.
///
/// @return This function returns no value.
static void namesPutShortEntry(unsigned int slot, const char *shortName,
  uint8_t ntReserved
) {
  Fat32DirectoryEntry entry;
  memset(&entry, 0, sizeof(entry));
  memcpy(entry.name, shortName, FAT32_SHORT_NAME_LENGTH);
  entry.attributes = FAT32_ATTR_ARCHIVE;
  entry.ntReserved = ntReserved;
  memcpy(&namesDisk[NAMES_DATA_SECTOR][slot * FAT32_DIRECTORY_ENTRY_SIZE],
    &entry, sizeof(entry));
}

/// @fn static void namesFixture(void)
///
/// @brief Build a one-sector root directory that holds "hostname" and
/// "README.TXT" as short names only, and "looseLoop" with a long name whose
/// short name is "LOOSEL~1".
///
/// @return This function returns no value.
static void namesFixture(void) {
  memset(namesDisk, 0, sizeof(namesDisk));
  uint32_t endOfChain = FAT32_FAT_ENTRY_MASK;
  memcpy(&namesDisk[NAMES_FAT_SECTOR][2 * sizeof(uint32_t)],
    &endOfChain, sizeof(endOfChain));

  namesBlockDevice.context = NULL;
  namesBlockDevice.readBlocks = namesReadBlocks;
  namesBlockDevice.writeBlocks = namesWriteBlocks;
  namesBlockDevice.blockSize = NAMES_SECTOR_SIZE;

  memset(&namesState, 0, sizeof(namesState));
  namesState.blockDevice = &namesBlockDevice;
  namesState.blockBuffer = namesBlockBuffer;
  namesState.blockSize = NAMES_SECTOR_SIZE;

  memset(&namesDriver, 0, sizeof(namesDriver));
  namesDriver.filesystemState = &namesState;
  namesDriver.bytesPerSector = NAMES_SECTOR_SIZE;
  namesDriver.sectorsPerCluster = 1;
  namesDriver.bytesPerCluster = NAMES_SECTOR_SIZE;
  namesDriver.fatStartSector = NAMES_FAT_SECTOR;
  namesDriver.fatSizeInSectors = 1;
  namesDriver.numberOfFats = 1;
  namesDriver.dataStartSector = NAMES_DATA_SECTOR;
  namesDriver.rootDirectoryCluster = FAT32_CLUSTER_FIRST_VALID;
  namesDriver.totalDataClusters = NAMES_NUM_SECTORS - NAMES_DATA_SECTOR;

  namesPutShortEntry(0, "HOSTNAME   ", FAT32_NT_RESERVED_LOWER_BASE);
  namesPutShortEntry(1, "README  TXT", 0);

  Fat32DirectoryEntry entryTemplate;
  memset(&entryTemplate, 0, sizeof(entryTemplate));
  entryTemplate.attributes = FAT32_ATTR_ARCHIVE;
  Fat32DirSearchResult created;
  memset(&created, 0, sizeof(created));
  NANO_OS_ASSERT_EQ_INT(FAT32_SUCCESS, fat32CreateFileEntry(&namesDriver,
    FAT32_CLUSTER_FIRST_VALID, "looseLoop", &entryTemplate, &created));
  free(created.longName);
}

/// @fn static int namesFind(const char *name, bool *caseConflict,
///   Fat32DirSearchResult *result)
///
/// @brief Search the fixture's root directory for a name.
///
/// @return Returns the status fat32SearchDirectoryN returned.
static int namesFind(const char *name, bool *caseConflict,
  Fat32DirSearchResult *result
) {
  memset(result, 0, sizeof(*result));
  return fat32SearchDirectoryN(&namesDriver, FAT32_CLUSTER_FIRST_VALID, name,
    strlen(name), false, result, caseConflict);
}

/// @fn static void expectExact(const char *name, int expectedStatus)
///
/// @brief Look a name up exactly and check whether it was found.
///
/// @return This function returns no value.
static void expectExact(const char *name, int expectedStatus) {
  Fat32DirSearchResult result;
  NANO_OS_ASSERT_EQ_INT(expectedStatus, namesFind(name, NULL, &result));
}

/// @fn static void expectCollision(const char *name, const char *shortName,
///   bool expectedConflict)
///
/// @brief Search for the entry a name collides with, and check which entry
/// was found and whether it differs from the name.
///
/// @return This function returns no value.
static void expectCollision(const char *name, const char *shortName,
  bool expectedConflict
) {
  Fat32DirSearchResult result;
  bool caseConflict = !expectedConflict;
  NANO_OS_ASSERT_EQ_INT(FAT32_SUCCESS, namesFind(name, &caseConflict, &result));
  NANO_OS_ASSERT_EQ_INT(0,
    memcmp(shortName, result.entry.name, FAT32_SHORT_NAME_LENGTH));
  NANO_OS_ASSERT_EQ_INT(expectedConflict, caseConflict);
}

NANO_OS_TEST(unit_fat32_names, long_name_matches_only_exactly) {
  namesFixture();
  expectExact("looseLoop", FAT32_SUCCESS);
  expectExact("looseloop", FAT32_FILE_NOT_FOUND);
  expectExact("LOOSELOOP", FAT32_FILE_NOT_FOUND);
}

NANO_OS_TEST(unit_fat32_names, short_alias_of_a_long_name_does_not_match) {
  namesFixture();
  expectExact("LOOSEL~1", FAT32_FILE_NOT_FOUND);
  expectExact("loosel~1", FAT32_FILE_NOT_FOUND);
}

NANO_OS_TEST(unit_fat32_names, short_only_name_matches_as_rendered) {
  namesFixture();
  expectExact("hostname", FAT32_SUCCESS);
  expectExact("HOSTNAME", FAT32_FILE_NOT_FOUND);
  expectExact("README.TXT", FAT32_SUCCESS);
  expectExact("readme.txt", FAT32_FILE_NOT_FOUND);
}

NANO_OS_TEST(unit_fat32_names, collision_search_finds_the_exact_name) {
  namesFixture();
  expectCollision("looseLoop", "LOOSEL~1   ", false);
  expectCollision("hostname", "HOSTNAME   ", false);
}

NANO_OS_TEST(unit_fat32_names, collision_search_finds_case_variants) {
  namesFixture();
  expectCollision("LOOSELOOP", "LOOSEL~1   ", true);
  expectCollision("Hostname", "HOSTNAME   ", true);
  expectCollision("readme.txt", "README  TXT", true);
}

NANO_OS_TEST(unit_fat32_names, collision_search_finds_a_short_alias) {
  namesFixture();
  expectCollision("LOOSEL~1", "LOOSEL~1   ", true);
  expectCollision("loosel~1", "LOOSEL~1   ", true);
}

NANO_OS_TEST(unit_fat32_names, collision_search_reports_no_conflict_when_absent) {
  namesFixture();
  Fat32DirSearchResult result;
  bool caseConflict = true;
  NANO_OS_ASSERT_EQ_INT(FAT32_FILE_NOT_FOUND,
    namesFind("missing", &caseConflict, &result));
  NANO_OS_ASSERT_FALSE(caseConflict);
}
