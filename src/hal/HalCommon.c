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

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn BlockDevice* halCommonInitRootSdSpiStorage(
///   SdCardSpiArgs *sdCardSpiArgs)
///
/// @brief Common routine for initializing root storage using an SD card over
/// SPI.
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the values to pass to runSdCardSpi.
///
/// @return Returns 0 on success, -errno on failure.
BlockDevice* halCommonInitRootSdSpiStorage(
  SdCardSpiArgs *sdCardSpiArgs
) {
  ProcessDescriptor *allProcesses = SCHEDULER_STATE->allProcesses;
  
  // Create the SD card process.
  ProcessDescriptor *processDescriptor
    = &allProcesses[SCHEDULER_STATE->firstUserPid - 1];
  if (processCreate(
    processDescriptor, runSdCardSpi, sdCardSpiArgs)
    != processSuccess
  ) {
    printString("Could not start SD card process\n");
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->firstUserPid;
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->restartFunction = HAL->blockDevice->restart;
  processDescriptor->restartArgs = (void*)(intptr_t)0;
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    allProcesses[SCHEDULER_STATE->firstUserPid - 1].mainThread, NULL);
  sdDevice->partitionNumber = 1;
  SCHEDULER_STATE->firstUserPid++;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;
  
  return sdDevice;
}

/// @fn int halCommonInitRootFilesystem(void)
///
/// @brief Common initialization for the root filesystem process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitRootFilesystem(void) {
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }
  
  BlockDevice *rootBlockDevice = HAL->blockDevice->get(0);
  
  if (rootBlockDevice == NULL) {
    if (HAL->blockDevice == NULL) {
      printString("ERROR! HAL->blockDevice is NULL\n");
      return -ENODEV;
    }
    
    if (HAL->blockDevice->init() != 0) {
      printString("ERROR! HAL->blockDevice->init() failed\n");
      return -ENODEV;
    }
    
    rootBlockDevice = HAL->blockDevice->get(0);
  }
  
  if (rootBlockDevice == NULL) {
    printString("ERROR! No rootBlockDevice available\n");
    return -ENODEV;
  }
  
  // Allocate the filesystem process.
  SCHEDULER_STATE->rootFsPid = SCHEDULER_STATE->firstUserPid;
  ProcessDescriptor *allProcesses = SCHEDULER_STATE->allProcesses;
  ProcessDescriptor *processDescriptor
    = &allProcesses[SCHEDULER_STATE->rootFsPid - 1];
  if (processCreate(processDescriptor, dummyProcess, NULL) != processSuccess) {
    printString("Could not allocate filesystem process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->rootFsPid;
  processDescriptor->name = "filesystem";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->restartFunction = restartFilesystem;
  // DO NOT resume the process yet.  Let the scheduler take care of that.
  
  SCHEDULER_STATE->firstUserPid = SCHEDULER_STATE->rootFsPid + 1;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;
  
  // The filesystem runs as an overlay, which increases the depth of the call
  // stack.  Double the stack size for it.
  Thread *thread = threadProvision(NULL, dummyProcess, NULL);
  if (thread == NULL) {
    printString("Could not increase filesystem process's stack size.\n");
    return -ENOMEM;
  }
  if (threadSetStackEnd(
    processDescriptor->mainThread, threadStackEnd(thread)) != processSuccess
  ) {
    printString("Could not set filesystem process's stack size.\n");
  }
  
  return 0;
}

/// @fn int restartFilesystem(ProcessDescriptor *processDescriptor)
///
/// @brief Restart the filesystem process using the existing root block device.
///
/// @param processDescriptor A pointer to the ProcessDescriptor of the
///   filesystem process to restart.
///
/// @return Returns 0 on success, -errno on failure.
int restartFilesystem(ProcessDescriptor *processDescriptor) {
  BlockDevice *rootBlockDevice = HAL->blockDevice->get(0);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
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

  //// BlockOverlayArgs blockOverlayArgs = {
  ////   .blockDevice = rootBlockDevice,
  ////   .startBlock = 1,
  ////   .args = &fs,
  //// };
  //// if (processCreate(processDescriptor, runBlockOverlay, &blockOverlayArgs)
  ////   != processSuccess
  //// ) {
  ////   printString("Could not restart filesystem process.\n");
  ////   return -ENOMEM;
  //// }
  if (processCreate(processDescriptor, runFilesystem, &fs)
    != processSuccess
  ) {
    printString("Could not restart filesystem process.\n");
    return -ENOMEM;
  }

  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = "filesystem";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->restartFunction = restartFilesystem;
  processDescriptor->callOverlayFunction = callOverlayFunctionFromBlockDevice;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.
  while (fs.driverState == NULL) {
    SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

/// @fn int halCommonInit(const Hal *hal)
///
/// @brief Initialization function common to multiple HAL implementations.
///
/// @param hal A pointer to an initialized Hal structure.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInit(const Hal *hal) {
  if (hal == NULL) {
    // Invalid parameter.
    return -EINVAL;
  }
  
  int32_t ii = 0;
  int32_t defaultUart = -1;
  
  if (hal->uart != NULL) {
    if (hal->uart->init() < 0) {
      return -ENOTTY;
    }
    int32_t numUarts = hal->uart->numSupported;
    if (numUarts <= 0) {
      // Nothing we can do.
      return -ENOTTY;
    }
    
    for (ii = 0; ii < numUarts; ii++) {
      if (!online(hal->uart, ii)) {
        continue;
      }
      
      if (hal->uart->configure(ii, 1000000) == 0) {
        if (defaultUart < 0) {
          defaultUart = ii;
        }
      } else {
        setOffline(hal->uart, ii);
      }
    }
  }

  if (hal->dio != NULL) {
    if (hal->dio->init() != 0) {
      hal->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize DIO subsystem\n",
        strlen("WARNING: Failed to initialize DIO subsystem\n"));
    }
  }

  if (hal->spi != NULL) {
    if (hal->spi->init() != 0) {
      hal->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize SPI subsystem\n",
        strlen("WARNING: Failed to initialize SPI subsystem\n"));
    }
  }

  if (hal->clock != NULL) {
    if (hal->clock->init() != 0) {
      hal->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize clock subsystem\n",
        strlen("WARNING: Failed to initialize clock subsystem\n"));
    }
  }

  if (hal->timer != NULL)  {
    do {
      if (hal->timer->init() != 0) {
        hal->uart->write(defaultUart,
          (uint8_t*) "WARNING: Failed to initialize timer subsystem\n",
          strlen("WARNING: Failed to initialize timer subsystem\n"));
        break;
      }
      
      uint32_t online = hal->timer->online[0];
      for (ii = 0; ii < (int32_t) hal->timer->numSupported; ii++) {
        if (online(hal->timer, ii) == false) {
          continue;
        }
        
        if (hal->timer->initDevice(ii) < 0) {
          setOffline(hal->timer, ii);
        }
      }
      
      if (hal->timer->online[0] != online) {
        if (hal->uart != NULL) {
          hal->uart->write(defaultUart,
            (uint8_t*) "WARNING: Did not initialize all timers\n",
            strlen("WARNING: Did not initialize all timers\n"));
        }
      }
    } while (0);
  }
  
  return 0;
}

