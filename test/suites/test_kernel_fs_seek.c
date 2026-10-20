///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_fs_seek.c
///
/// @brief             Kernel tests for the filesystem's fseek command handler
///                    and the position ftell reports after it.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Filesystem.h"
#include "kernel/Processes.h"
#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/drivers/fat32/Fat32.h"

void* SeekFile(void *args);

static Fat32DriverState seekDriverState;
static Fat32FileHandle seekHandle;
static NanoOsFile seekFile;
static FilesystemState seekState;
static ProcessMessage seekMessage;
static FilesystemSeekArgs seekArgs;

/// @fn static void seekFixture(uint32_t fileSize, uint32_t position)
///
/// @brief Open a file with no clusters allocated, so a seek never has to
/// read the FAT, at the given size and position.
static void seekFixture(uint32_t fileSize, uint32_t position) {
  memset(&seekDriverState, 0, sizeof(seekDriverState));
  memset(&seekHandle, 0, sizeof(seekHandle));
  seekHandle.fileSize = fileSize;
  seekHandle.currentPosition = position;
  memset(&seekFile, 0, sizeof(seekFile));
  seekFile.file = &seekHandle;
  seekFile.currentPosition = position;
  memset(&seekState, 0, sizeof(seekState));
  seekState.driverState = &seekDriverState;
  seekState.args = &seekMessage;
}

/// @fn static void seek(long offset, int whence)
///
/// @brief Run the fseek command handler on the fixture's file.
static void seek(long offset, int whence) {
  memset(&seekArgs, 0, sizeof(seekArgs));
  seekArgs.stream = &seekFile;
  seekArgs.offset = offset;
  seekArgs.whence = whence;
  memset(&seekMessage, 0, sizeof(seekMessage));
  processMessageInit(&seekMessage, 0, &seekArgs, sizeof(seekArgs), false);
  SeekFile(&seekState);
  processMessageRelease(&seekMessage);
}

NANO_OS_KERNEL_TEST(fs_seek, seek_set_moves_ftell) {
  seekFixture(100, 0);
  seek(40, SEEK_SET);
  NANO_OS_ASSERT_EQ_INT(0, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(0, seekArgs.errorNumber);
  NANO_OS_ASSERT_EQ_INT(40, filesystemFtell(&seekFile));
  NANO_OS_ASSERT_EQ_INT(40, seekHandle.currentPosition);
}

NANO_OS_KERNEL_TEST(fs_seek, seek_cur_is_relative_to_the_position) {
  seekFixture(100, 30);
  seek(-10, SEEK_CUR);
  NANO_OS_ASSERT_EQ_INT(0, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(20, filesystemFtell(&seekFile));
}

NANO_OS_KERNEL_TEST(fs_seek, seek_end_is_relative_to_the_size) {
  seekFixture(100, 0);
  seek(-1, SEEK_END);
  NANO_OS_ASSERT_EQ_INT(0, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(99, filesystemFtell(&seekFile));
}

NANO_OS_KERNEL_TEST(fs_seek, seek_past_the_end_fails_and_keeps_the_position) {
  seekFixture(100, 30);
  seek(101, SEEK_SET);
  NANO_OS_ASSERT_EQ_INT(-1, seekArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(5, seekArgs.errorNumber); // EINVAL
  NANO_OS_ASSERT_EQ_INT(30, filesystemFtell(&seekFile));
  NANO_OS_ASSERT_EQ_INT(30, seekHandle.currentPosition);
}
