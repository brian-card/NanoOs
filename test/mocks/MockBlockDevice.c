///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockBlockDevice.c
///
/// @brief             Block-device layer for the mock HAL.
///
/// Currently only MOCK_STORAGE_NONE is implemented: the kernel boots with no
/// root filesystem (SCHEDULER_STATE->rootFsPid == 0, which schedFopen and
/// friends already handle by returning NULL/ENOENT).  MOCK_STORAGE_FILE - a
/// POSIX SD-card process backed by a real FAT32 image, reusing the same
/// runSdCardPosix process buildsim uses - is stubbed and will be filled in
/// when filesystem/pipe suites are added.
///
///////////////////////////////////////////////////////////////////////////////

#include "HalMock.h"
#include "MockSubsystems.h"

// -ENODEV / -ENOSYS without pulling in a NanoOs errno header that remaps names.
#ifndef ENODEV
#define ENODEV 19
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif

void mockBlockDeviceReset(void) {
}

int32_t mockBlockDeviceInitFn(va_list args) {
  (void) args;
  return -ENODEV;
}

int32_t mockBlockDeviceGetFn(va_list args) {
  (void) va_arg(args, int32_t); // deviceId
  void **returnValue = va_arg(args, void**);
  if (returnValue != NULL) {
    *returnValue = NULL;
  }
  return -ENODEV;
}

int32_t mockBlockDeviceRestartFn(va_list args) {
  (void) args;
  return -ENOSYS;
}
