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

/// @file FormatDriver.c
///
/// @brief Implementation of driverFormat for FAT32:  writes a fresh BPB
/// (BIOS Parameter Block), FSInfo sector, both FAT copies, and an empty root
/// directory to a partition.

// Standard C includes
#include <string.h>

// NanoOs includes
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#endif // NANO_OS_KERNEL_BUILD
#include "Fat32.h"

// KEEP_IN_FLASH keeps a global variable out of .data/.bss and in a part of
// the binary that isn't stripped on targets that remove .rodata post-link
// (see docs/2026-08-02_Saving-Space.md).  It's normally pulled in from
// NanoOs.h, but this file is shared with freestanding userspace builds that
// don't have that header on their include path, so fall back to a local
// definition when it isn't already in scope.
#ifndef KEEP_IN_FLASH
#define KEEP_IN_FLASH __attribute__((section(".text")))
#endif // KEEP_IN_FLASH

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Choose the default FAT32 cluster size for a partition, following
///        Microsoft's published cluster-size table.
///
/// @param totalSectors    Total number of sectors in the partition.
/// @param bytesPerSector  The block device's sector size in bytes.
///
/// @return The default sectors-per-cluster value, or 0 if the partition is
///         too small to hold a FAT32 filesystem.
///
static uint8_t fat32DefaultSectorsPerCluster(
    uint32_t totalSectors,
    uint16_t bytesPerSector
) {
  // Thresholds below are in sectors, derived from Microsoft's default cluster
  // size table (which is specified in MB):
  //
  //   < 32 MB   — not supported as FAT32
  //   < 64 MB   — 512 B  (1 sector)
  //   < 128 MB  — 1 KB   (2 sectors)
  //   < 256 MB  — 2 KB   (4 sectors)
  //   < 8 GB    — 4 KB   (8 sectors)
  //   < 16 GB   — 8 KB   (16 sectors)
  //   < 32 GB   — 16 KB  (32 sectors)
  //   >= 32 GB  — 32 KB  (64 sectors)
  //
  // All thresholds assume 512-byte sectors; scale by bytesPerSector for
  // non-standard sector sizes.
  uint32_t scale = (uint32_t) bytesPerSector / FAT32_SECTOR_SIZE;
  if (scale == 0) {
    scale = 1;
  }

  // Sector counts for each threshold (at 512 B/sector).
  // 32 MB  = 32 * 1024 * 1024 / 512
  if (totalSectors < (65536UL / scale)) {      // < 32 MB
    return 0;  // Too small for FAT32
  }
  if (totalSectors < (131072UL / scale)) {     // < 64 MB
    return 1;
  }
  if (totalSectors < (262144UL / scale)) {     // < 128 MB
    return 2;
  }
  if (totalSectors < (524288UL / scale)) {     // < 256 MB
    return 4;
  }
  if (totalSectors < (16777216UL / scale)) {   // < 8 GB
    return 8;
  }
  if (totalSectors < (33554432UL / scale)) {   // < 16 GB
    return 16;
  }
  if (totalSectors < (67108864UL / scale)) {   // < 32 GB
    return 32;
  }
  return 64;
}

/// @var _fat32OemName
///
/// @brief 8-byte OEM name written into the BPB oemName field of a newly
/// formatted FAT32 boot sector.
static const char _fat32OemName[] KEEP_IN_FLASH = "MSDOS5.0";

/// @var _fat32FileSystemType
///
/// @brief 8-byte filesystem type string written into the BPB
/// fileSystemType field of a newly formatted FAT32 boot sector.
static const char _fat32FileSystemType[] KEEP_IN_FLASH = "FAT32   ";

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Format a partition as FAT32, writing a valid BPB (BIOS Parameter
///        Block), FSInfo sector, both FAT copies, and an empty root directory.
///
/// @details On return the partition is ready to be mounted with
///          filesystemInitDriver().  The caller must have already called
///          getPartitionInfoImpl() so that filesystemState->startLba and
///          filesystemState->endLba are valid.  filesystemState->blockBuffer
///          must point to a buffer of at least blockDevice->blockSize bytes.
///
///          The volume label is written space-padded to 11 bytes in the BPB.
///          Pass an empty string or NULL for no label.
///
///          @p clusterSize specifies the desired cluster size in bytes.  It
///          must be a power of two in the range [FAT32_CLUSTER_SIZE_MIN,
///          FAT32_CLUSTER_SIZE_MAX] and must be a multiple of bytesPerSector.
///          Pass 0 to use a default size derived from the partition size
///          following Microsoft's published FAT32 cluster size table.
///
/// @param filesystemState  Pointer to a FilesystemState whose blockDevice,
///                         startLba, and endLba fields are already valid.
/// @param volumeLabel      Null-terminated volume label (up to 11 characters).
///                         May be NULL.
/// @param clusterSize      Desired cluster size in bytes, or 0 for the default.
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if the cluster
///         size is invalid, FAT32_INVALID_FILESYSTEM if the partition is too
///         small or the block device reports an unusable sector size, or
///         FAT32_ERROR on an I/O failure.
///
int driverFormat(
    FilesystemState *filesystemState,
    const char      *volumeLabel,
    uint32_t         clusterSize
) {
  if (filesystemState == NULL) {
    return FAT32_INVALID_PARAMETER;
  }

  BlockDevice *bd = filesystemState->blockDevice;
  if ((bd == NULL) || (filesystemState->blockBuffer == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Derive basic geometry ----
  uint16_t bytesPerSector = (uint16_t) bd->blockSize;
  if ((bytesPerSector < FAT32_SECTOR_SIZE)
      || ((bytesPerSector & (bytesPerSector - 1)) != 0)
  ) {
    return FAT32_INVALID_FILESYSTEM;
  }

  uint32_t totalSectors =
    filesystemState->endLba - filesystemState->startLba + 1;

  // ---- Resolve the cluster size ----
  uint8_t sectorsPerCluster;

  if (clusterSize == 0) {
    sectorsPerCluster = fat32DefaultSectorsPerCluster(
      totalSectors, bytesPerSector);
    if (sectorsPerCluster == 0) {
      return FAT32_INVALID_FILESYSTEM;  // Partition too small for FAT32
    }
  } else {
    // Validate the caller-supplied size.
    if ((clusterSize < FAT32_CLUSTER_SIZE_MIN)
        || (clusterSize > FAT32_CLUSTER_SIZE_MAX)
        || (clusterSize % bytesPerSector != 0)
        || ((clusterSize & (clusterSize - 1)) != 0)
    ) {
      return FAT32_INVALID_PARAMETER;
    }
    sectorsPerCluster = (uint8_t) (clusterSize / bytesPerSector);
  }

  uint32_t bytesPerCluster =
    (uint32_t) sectorsPerCluster * (uint32_t) bytesPerSector;

  // Verify the resolved cluster size is within FAT32 bounds.  This catches
  // the case where a large bytesPerSector causes the default or caller-
  // supplied sectorsPerCluster to produce an out-of-range result.
  if ((bytesPerCluster < FAT32_CLUSTER_SIZE_MIN)
      || (bytesPerCluster > FAT32_CLUSTER_SIZE_MAX)
  ) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Compute FAT layout ----
  //
  // The standard reserved sector count for FAT32 is 32; this places the BPB
  // in sector 0, FSInfo in sector 1, and a backup boot sector in sector 6,
  // with room for the remaining backup sectors.
  const uint16_t reservedSectorCount = 32;
  const uint16_t fsInfoSector        = 1;
  const uint16_t backupBootSector    = 6;
  const uint8_t  numberOfFats        = 2;
  const uint32_t rootCluster         = FAT32_CLUSTER_FIRST_VALID;  // cluster 2

  // FAT size: each cluster requires one 4-byte FAT entry.  Round up.
  //
  //   fatSizeInSectors = ceil((totalDataClusters + 2) * 4 / bytesPerSector)
  //
  // The "+2" accounts for the two reserved FAT entries (clusters 0 and 1).
  // We need to iterate once because totalDataClusters depends on fatSize.
  //
  // Use the standard formula (from the FAT specification):
  //
  //   RootDirSectors = 0  (FAT32 root is a cluster chain, not fixed)
  //   TmpVal1 = totalSectors - reservedSectorCount
  //   TmpVal2 = (256 * sectorsPerCluster) + numberOfFats * (4/4)
  //           = 256 * sectorsPerCluster + numberOfFats / (bytesPerSector / 4)
  //
  // Simplified integer version:
  uint32_t dataSectorsApprox = totalSectors - reservedSectorCount;

  // fatSizeInSectors = ceil(dataSectors * 4 / (bytesPerSector * spc + 2 * 4))
  // Use the formula from the FAT spec white paper (section 3.3):
  uint32_t numerator   = 4UL * dataSectorsApprox + 8UL;  // "+8" for 2 reserved
  uint32_t denominator =
    (uint32_t) bytesPerSector * (uint32_t) sectorsPerCluster
    + (uint32_t) numberOfFats * 4UL;
  uint32_t fatSizeInSectors = (numerator + denominator - 1) / denominator;

  uint32_t fatStartSector = filesystemState->startLba + reservedSectorCount;
  uint32_t dataStartSector =
    fatStartSector + (uint32_t) numberOfFats * fatSizeInSectors;

  // Verify that the partition is large enough to hold the FAT and at least
  // one data cluster (for the root directory).
  if (dataStartSector + (uint32_t) sectorsPerCluster
      > filesystemState->endLba + 1
  ) {
    return FAT32_INVALID_FILESYSTEM;
  }

  uint32_t dataSectors =
    (filesystemState->endLba + 1) - dataStartSector;
  uint32_t totalDataClusters = dataSectors / sectorsPerCluster;

  // ---- Write the BPB / boot sector (sector 0 of partition) ----
  memset(filesystemState->blockBuffer, 0, bytesPerSector);

  Fat32BiosParameterBlock *bpb =
    (Fat32BiosParameterBlock *) filesystemState->blockBuffer;

  // Jump instruction: EB 58 90  (jmp short +0x58; nop)
  bpb->jumpBoot[0] = 0xEB;
  bpb->jumpBoot[1] = 0x58;
  bpb->jumpBoot[2] = 0x90;

  memcpy(bpb->oemName, _fat32OemName, 8);

  // BPB fields — written via memcpy to avoid unaligned-access faults on
  // strict-alignment targets (ARM Cortex-M0+, eZ80).
  {
    uint16_t bps = bytesPerSector;
    memcpy(&bpb->bytesPerSector, &bps, sizeof(uint16_t));
  }
  bpb->sectorsPerCluster = sectorsPerCluster;
  {
    uint16_t rsc = reservedSectorCount;
    memcpy(&bpb->reservedSectorCount, &rsc, sizeof(uint16_t));
  }
  bpb->numberOfFats = numberOfFats;
  {
    uint16_t zero16 = 0;
    memcpy(&bpb->rootEntryCount, &zero16, sizeof(uint16_t));
    memcpy(&bpb->totalSectors16, &zero16, sizeof(uint16_t));
    memcpy(&bpb->fatSize16,      &zero16, sizeof(uint16_t));
  }
  bpb->mediaType = 0xF8;  // Fixed disk
  {
    uint16_t spt = 63;
    uint16_t noh = 255;
    memcpy(&bpb->sectorsPerTrack, &spt, sizeof(uint16_t));
    memcpy(&bpb->numberOfHeads,   &noh, sizeof(uint16_t));
  }
  {
    uint32_t hs = filesystemState->startLba;  // Hidden sectors = LBA offset
    memcpy(&bpb->hiddenSectors, &hs, sizeof(uint32_t));
  }
  {
    uint32_t ts32 = totalSectors;
    memcpy(&bpb->totalSectors32, &ts32, sizeof(uint32_t));
  }

  // FAT32-specific extension (offset 36)
  {
    uint32_t fs32 = fatSizeInSectors;
    memcpy(&bpb->fatSize32, &fs32, sizeof(uint32_t));
  }
  {
    uint16_t extFlags = 0;
    memcpy(&bpb->extFlags, &extFlags, sizeof(uint16_t));
  }
  {
    uint16_t fsVer = 0x0000;
    memcpy(&bpb->fileSystemVersion, &fsVer, sizeof(uint16_t));
  }
  {
    uint32_t rc = rootCluster;
    memcpy(&bpb->rootCluster, &rc, sizeof(uint32_t));
  }
  {
    uint16_t fis = fsInfoSector;
    memcpy(&bpb->fsInfoSector, &fis, sizeof(uint16_t));
  }
  {
    uint16_t bbs = backupBootSector;
    memcpy(&bpb->backupBootSector, &bbs, sizeof(uint16_t));
  }
  memset(bpb->reserved,  0, sizeof(bpb->reserved));
  bpb->driveNumber   = 0x80;
  bpb->reserved1     = 0x00;
  bpb->bootSignature = 0x29;
  {
    // A trivial but non-zero serial number derived from geometry.
    uint32_t vsn = totalSectors ^ ((uint32_t) bytesPerSector << 16);
    memcpy(&bpb->volumeSerialNumber, &vsn, sizeof(uint32_t));
  }

  // Volume label — space-padded, upper-case, 11 bytes.
  memset(bpb->volumeLabel, ' ', sizeof(bpb->volumeLabel));
  if ((volumeLabel != NULL) && (volumeLabel[0] != '\0')) {
    for (int i = 0; (i < 11) && (volumeLabel[i] != '\0'); i++) {
      char c = volumeLabel[i];
      if ((c >= 'a') && (c <= 'z')) {
        c -= 32;
      }
      bpb->volumeLabel[i] = (uint8_t) c;
    }
  }

  memcpy(bpb->fileSystemType, _fat32FileSystemType, 8);
  memset(bpb->bootCode, 0, sizeof(bpb->bootCode));
  {
    uint16_t sig = 0xAA55;
    memcpy(&bpb->signatureWord, &sig, sizeof(uint16_t));
  }

  // Write boot sector.
  int result = bd->writeBlocks(
    bd->context, filesystemState->startLba, 1, bd->blockSize,
    filesystemState->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  // Write backup boot sector at sector 6 of the partition.
  result = bd->writeBlocks(
    bd->context, filesystemState->startLba + backupBootSector,
    1, bd->blockSize, filesystemState->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  // ---- Write the FSInfo sector ----
  memset(filesystemState->blockBuffer, 0, bytesPerSector);

  Fat32FsInfoSector *fsi =
    (Fat32FsInfoSector *) filesystemState->blockBuffer;

  {
    uint32_t leadSig   = FAT32_FSINFO_LEAD_SIG;
    uint32_t structSig = FAT32_FSINFO_STRUCT_SIG;
    // Free count: totalDataClusters minus the one root directory cluster.
    uint32_t freeCount = totalDataClusters - 1;
    // Next free hint: cluster 3 (cluster 2 was just allocated for root).
    uint32_t nextFree  = rootCluster + 1;
    uint32_t trailSig  = FAT32_FSINFO_TRAIL_SIG;

    memcpy(&fsi->leadSignature,   &leadSig,   sizeof(uint32_t));
    memset(fsi->reserved1, 0, sizeof(fsi->reserved1));
    memcpy(&fsi->structSignature, &structSig, sizeof(uint32_t));
    memcpy(&fsi->freeCount,       &freeCount, sizeof(uint32_t));
    memcpy(&fsi->nextFree,        &nextFree,  sizeof(uint32_t));
    memset(fsi->reserved2, 0, sizeof(fsi->reserved2));
    memcpy(&fsi->trailSignature,  &trailSig,  sizeof(uint32_t));
  }

  result = bd->writeBlocks(
    bd->context,
    filesystemState->startLba + fsInfoSector,
    1, bd->blockSize, filesystemState->blockBuffer);
  if (result != 0) {
    return FAT32_ERROR;
  }

  // ---- Zero all reserved sectors between 0 and reservedSectorCount ----
  // Sector 0 and fsInfoSector are already written; wipe the rest so that
  // stale data from a previous format can't confuse a future
  // filesystemInitDriver.
  memset(filesystemState->blockBuffer, 0, bytesPerSector);
  for (uint16_t s = 2; s < reservedSectorCount; s++) {
    if (s == backupBootSector) {
      continue;  // Already written above.
    }
    result = bd->writeBlocks(
      bd->context, filesystemState->startLba + s,
      1, bd->blockSize, filesystemState->blockBuffer);
    if (result != 0) {
      return FAT32_ERROR;
    }
  }

  // ---- Write both FAT copies ----
  //
  // Walk every sector of the FAT region.  For most sectors the content is
  // all-zero (free clusters).  The first sector of FAT1 (and its mirror in
  // FAT2) must be initialised with:
  //
  //   Entry 0: 0x0FFFFF??  (media byte in low byte, 0xFFFFF? in high)
  //   Entry 1: 0x0FFFFFFF  (end-of-chain)
  //   Entry 2: 0x0FFFFFFF  (root directory cluster, EOC)
  //
  // All remaining entries are 0x00000000 (free).
  for (uint8_t fatIndex = 0; fatIndex < numberOfFats; fatIndex++) {
    uint32_t thisFatStart =
      fatStartSector + (uint32_t) fatIndex * fatSizeInSectors;

    for (uint32_t sector = 0; sector < fatSizeInSectors; sector++) {
      memset(filesystemState->blockBuffer, 0, bytesPerSector);

      if (sector == 0) {
        // Initialise the three reserved entries in the first FAT sector.
        uint32_t entry0 = 0x0FFFFF00UL | 0xF8UL;  // Media byte
        uint32_t entry1 = FAT32_CLUSTER_EOC;
        uint32_t entry2 = FAT32_CLUSTER_EOC;       // Root directory
        memcpy(filesystemState->blockBuffer + 0, &entry0, sizeof(uint32_t));
        memcpy(filesystemState->blockBuffer + 4, &entry1, sizeof(uint32_t));
        memcpy(filesystemState->blockBuffer + 8, &entry2, sizeof(uint32_t));
      }

      result = bd->writeBlocks(
        bd->context, thisFatStart + sector, 1, bd->blockSize,
        filesystemState->blockBuffer);
      if (result != 0) {
        return FAT32_ERROR;
      }
    }
  }

  // ---- Zero the root directory cluster ----
  //
  // The root directory occupies cluster 2 (rootCluster).  Its sectors must
  // be zeroed so that fat32SearchDirectory stops at the first entry, which
  // will have a 0x00 first byte (FAT32_ENTRY_END_OF_DIR).
  {
    uint32_t rootLba = dataStartSector
      + (rootCluster - FAT32_CLUSTER_FIRST_VALID)
      * (uint32_t) sectorsPerCluster;

    memset(filesystemState->blockBuffer, 0, bytesPerSector);
    for (uint8_t s = 0; s < sectorsPerCluster; s++) {
      result = bd->writeBlocks(
        bd->context, rootLba + s, 1, bd->blockSize,
        filesystemState->blockBuffer);
      if (result != 0) {
        return FAT32_ERROR;
      }
    }
  }

  return FAT32_SUCCESS;
}
