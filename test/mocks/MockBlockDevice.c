///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockBlockDevice.c
///
/// @brief             Block-device layer for the mock HAL.
///
/// Currently only MOCK_STORAGE_NONE is implemented: the kernel boots with no
/// root filesystem (rootFilesystemPid == 0, which schedFopen and
/// friends already handle by returning NULL/ENOENT).  MOCK_STORAGE_FILE - a
/// POSIX SD card backed by a real FAT32 image, reusing the same
/// sdCardPosixInit buildsim uses - is stubbed and will be filled in
/// when filesystem/pipe suites are added.
///
///////////////////////////////////////////////////////////////////////////////

#include "HalMock.h"
#include "MockSubsystems.h"

// -ENODEV without pulling in a NanoOs errno header that remaps names.
#ifndef ENODEV
#define ENODEV 19
#endif

void mockBlockDeviceReset(void) {
}

int mockBlockDeviceInitFn(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}

int mockBlockDeviceGetFn(int32_t deviceId, va_list args) {
  (void) deviceId;
  void **returnValue = va_arg(args, void**);
  if (returnValue != NULL) {
    *returnValue = NULL;
  }
  return -ENODEV;
}

/// @fn int mockBlockDeviceReadBlocksFn(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_READ_BLOCKS for the mock HAL, which has no storage.
///
/// @param deviceId Ignored.
/// @param args Ignored.
///
/// @return Returns -ENODEV always.
int mockBlockDeviceReadBlocksFn(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}

/// @fn int mockBlockDeviceWriteBlocksFn(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_WRITE_BLOCKS for the mock HAL, which has no
/// storage.
///
/// @param deviceId Ignored.
/// @param args Ignored.
///
/// @return Returns -ENODEV always.
int mockBlockDeviceWriteBlocksFn(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}
