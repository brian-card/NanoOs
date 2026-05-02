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

/// @file HalCommon.c
///
/// @brief HAL routines that are common to multiple implementations.

#include "HalCommon.h"
#include "../kernel/Fat32Filesystem.h"
#include "../kernel/Filesystem.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn BlockStorageDevice* halCommonInitRootSdSpiStorage(
///   SdCardSpiArgs *sdCardSpiArgs)
///
/// @brief Common routine for initializing root storage using an SD card over
/// SPI.
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the values to pass to runSdCardSpi.
///
/// @return Returns 0 on success, -errno on failure.
BlockStorageDevice* halCommonInitRootSdSpiStorage(
  SdCardSpiArgs *sdCardSpiArgs
) {
  TaskDescriptor *allTasks = SCHEDULER_STATE->allTasks;
  
  // Create the SD card task.
  TaskDescriptor *taskDescriptor
    = &allTasks[SCHEDULER_STATE->firstUserTaskId - 1];
  if (taskCreate(
    taskDescriptor, runSdCardSpi, sdCardSpiArgs)
    != taskSuccess
  ) {
    printString("Could not start SD card task\n");
    return NULL;
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = SCHEDULER_STATE->firstUserTaskId;
  taskDescriptor->name = "SD card";
  taskDescriptor->userId = ROOT_USER_ID;
  BlockStorageDevice *sdDevice = (BlockStorageDevice*) coroutineResume(
    allTasks[SCHEDULER_STATE->firstUserTaskId - 1].taskHandle, NULL);
  sdDevice->partitionNumber = 1;
  SCHEDULER_STATE->firstUserTaskId++;
  SCHEDULER_STATE->firstShellTaskId = SCHEDULER_STATE->firstUserTaskId;
  
  return sdDevice;
}

/// @fn int halCommonInitRootFilesystem(BlockStorageDevice *blockDevice)
///
/// @brief Common initialization for the root filesystem process.
///
/// @param blockDevice A pointer to a BlockStorageDevice initialized by a
///   block storage process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitRootFilesystem(BlockStorageDevice *blockDevice) {
  if (blockDevice == NULL) {
    printString("No BlockStorageDevice provided\n");
    return -ENODEV;
  }
  
  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = blockDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.driverInit = fat32Initialize;
  fs.driverFopen = fat32Fopen;
  fs.driverFread = fat32Fread;
  fs.driverFwrite = fat32Fwrite;
  fs.driverFclose = fat32Fclose;
  fs.driverRemove = fat32Remove;
  fs.driverFseek = fat32Fseek;
  fs.driverGetFileBlockMetadata = fat32GetFileBlockMetadata;
  fs.driverGetFilename = fat32GetFilename;
  
  // Create the filesystem task.
  SCHEDULER_STATE->rootFsTaskId = SCHEDULER_STATE->firstUserTaskId;
  TaskDescriptor *allTasks = SCHEDULER_STATE->allTasks;
  TaskDescriptor *taskDescriptor = &allTasks[SCHEDULER_STATE->rootFsTaskId - 1];
  if (taskCreate(taskDescriptor, runFilesystem, &fs)
    != taskSuccess
  ) {
    printString("Could not start filesystem task\n");
    return -ENOMEM;
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = SCHEDULER_STATE->rootFsTaskId;
  taskDescriptor->name = "filesystem";
  taskDescriptor->userId = ROOT_USER_ID;
  // Let it pick up the arguments
  taskResume(taskDescriptor, NULL);
  
  SCHEDULER_STATE->firstUserTaskId = SCHEDULER_STATE->rootFsTaskId + 1;
  SCHEDULER_STATE->firstShellTaskId = SCHEDULER_STATE->firstUserTaskId;
  
  return 0;
}

/// @fn int halCommonInit(Hal *hal)
///
/// @brief Initialization function common to multiple HAL implementations.
///
/// @param hal A pointer to an initialized Hal structure.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInit(Hal *hal) {
  int ii = 0;
  char num = '\0';
  
  if (hal->uartHal != NULL) {
    int numUarts = hal->uartHal->getNumUarts();
    if (numUarts <= 0) {
      // Nothing we can do.
      return -ENOTTY;
    }
    
    // Set all the serial ports to run at 1000000 baud.
    if (hal->uartHal->initUart(0, 1000000) < 0) {
      // Nothing we can do.
      return -EIO;
    }
    for (ii = 1; ii < numUarts; ii++) {
      if (hal->uartHal->initUart(ii, 1000000) < 0) {
        // We can't support more than the last serial port that was successfully
        // initialized.
        break;
      }
    }
    hal->uartHal->setNumUarts(ii);
    if (ii != numUarts) {
      // NOTE:  We can't use printString and printInt here because those
      // functions rely on the global HAL pointer, which is not initialized at
      // the time this function is called.  So, we have to do things a little
      // more manually here.
      hal->uartHal->writeUart(0,
        (uint8_t*) "WARNING: Only initialized ",
        strlen("WARNING: Only initialized "));
      num = '0' + ((char) ii);
      hal->uartHal->writeUart(0, (uint8_t*) &num, 1);
      hal->uartHal->writeUart(0,
        (uint8_t*) " serial ports\n",
        strlen(" serial ports\n"));
    }
  }

  if (hal->timerHal != NULL)  {
    int numTimers = hal->timerHal->getNumTimers();
    for (ii = 0; ii < numTimers; ii++) {
      if (hal->timerHal->initTimer(ii) < 0) {
        break;
      }
    }
    hal->timerHal->setNumTimers(ii);
    if (ii != numTimers) {
      hal->uartHal->writeUart(0,
        (uint8_t*) "WARNING: Only initialized ",
        strlen("WARNING: Only initialized "));
      num = '0' + ((char) ii);
      hal->uartHal->writeUart(0, (uint8_t*) &num, 1);
      hal->uartHal->writeUart(0,
        (uint8_t*) " timers\n",
        strlen(" timers\n"));
    }
  }
  
  return 0;
}

