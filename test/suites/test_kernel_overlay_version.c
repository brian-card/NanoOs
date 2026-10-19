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


/// @file test_kernel_overlay_version.c
///
/// @brief Kernel tests for the overlay version check in schedulerLoadOverlay.

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Overlay.h"
#include "kernel/Processes.h"
#include "user/NanoOsErrno.h"

// Defined in Overlay.c
extern NanoOsOverlayMap *overlayMap;

// Defined in Scheduler.c
int schedulerLoadOverlay(ProcessDescriptor *processDescriptor, char **envp);

/// @var _versionTestVersion
///
/// @brief The header version the fake block device's overlay reports.
static uint32_t _versionTestVersion = 0;

/// @fn static int versionTestReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Fill buffer with an overlay header carrying _versionTestVersion.
///
/// @param context Unused.
/// @param startBlock Unused.
/// @param numBlocks Unused.
/// @param blockSize Unused.
/// @param buffer The overlay memory to fill.
///
/// @return Returns 0.
static int versionTestReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  (void) context;
  (void) startBlock;
  (void) numBlocks;
  (void) blockSize;
  NanoOsOverlayMap *map = (NanoOsOverlayMap*) buffer;
  memset(&map->header, 0, sizeof(map->header));
  map->header.version = _versionTestVersion;
  map->header.magic = NANO_OS_OVERLAY_MAGIC;
  return 0;
}

/// @fn static int versionTestLoad(uint32_t version)
///
/// @brief Load an overlay reporting the given header version.
///
/// @param version The header version the overlay reports.
///
/// @return Returns the value returned by schedulerLoadOverlay.
static int versionTestLoad(uint32_t version) {
  static BlockDevice blockDevice = {
    .schedReadBlocks = versionTestReadBlocks,
    .blockSize = 512,
  };
  ProcessDescriptor processDescriptor;
  memset(&processDescriptor, 0, sizeof(processDescriptor));
  processDescriptor.privilegeLevel = PRIVILEGE_LEVEL_USER;
  processDescriptor.overlay.fileBlockMetadata.blockDevice = &blockDevice;
  processDescriptor.overlay.fileBlockMetadata.startBlock = 7;
  processDescriptor.overlay.fileBlockMetadata.numBlocks = 1;

  _versionTestVersion = version;
  return schedulerLoadOverlay(&processDescriptor, NULL);
}

NANO_OS_KERNEL_TEST(overlay_version, matching_version_loads) {
  NANO_OS_ASSERT_NOT_NULL(overlayMap);
  NANO_OS_ASSERT_EQ_INT(0, versionTestLoad(NANO_OS_OVERLAY_VERSION));
}

NANO_OS_KERNEL_TEST(overlay_version, previous_version_is_rejected) {
  NANO_OS_ASSERT_NOT_NULL(overlayMap);
  NANO_OS_ASSERT_EQ_INT(-ENOEXEC,
    versionTestLoad(NANO_OS_OVERLAY_VERSION - (((uint32_t) 1) << 8)));
}
