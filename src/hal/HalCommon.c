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

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn int halCommonInitRootSdSpiStorage(SdCardSpiArgs *sdCardSpiArgs)
///
/// @brief Common routine for initializing root storage using an SD card over
/// SPI.
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the values to pass to runSdCardSpi.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitRootSdSpiStorage(SdCardSpiArgs *sdCardSpiArgs) {
  TaskDescriptor *allTasks = SCHEDULER_STATE->allTasks;
  
  // Create the SD card task.
  TaskDescriptor *taskDescriptor
    = &allTasks[SCHEDULER_STATE->firstUserTaskId - 1];
  if (taskCreate(
    taskDescriptor, runSdCardSpi, sdCardSpiArgs)
    != taskSuccess
  ) {
    printString("Could not start SD card task.\n");
    return -ENOMEM;
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
  
  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = sdDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.driverInit = exFatInitialize;
  fs.driverOpenFile = exFatOpenFile;
  fs.driverRead = exFatRead;
  fs.driverWrite = exFatWrite;
  fs.driverFclose = exFatFclose;
  fs.driverRemove = exFatRemove;
  fs.driverSeek = exFatSeek;
  fs.driverGetFileBlockMetadata = exFatGetFileBlockMetadata;
  fs.driverGetFilename = exFatGetFilename;
  
  // Create the filesystem task.
  SCHEDULER_STATE->rootFsTaskId = SCHEDULER_STATE->firstUserTaskId;
  taskDescriptor = &allTasks[SCHEDULER_STATE->rootFsTaskId - 1];
  if (taskCreate(taskDescriptor, runExFatFilesystem, &fs)
    != taskSuccess
  ) {
    printString("Could not start filesystem task.\n");
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

