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

// Doxygen marker
/// @file

// Custom includes
#include "Scheduler.h"
#include "SdCard.h"
#include "NanoOs.h"
#include "Tasks.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn int sdCardGetReadWriteParameters(
///   SdCardState *sdCardState, SdCommandParams *sdCommandParams,
///   uint32_t *startSdBlock, uint32_t *numSdBlocks)
///
/// @brief Get the startSdBlock and numSdBlocks parameters for a read or write
/// operation on the SD card.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   SD card task.
/// @param sdCommandParams A pointer to the SdCommandParams structure passed in
///   by the client function.
/// @param startSdBlock A pointer to the uint32_t variable that will hold the
///   first block of the SD card to read from or write to.
/// @param numSdBlocks A pointer to the uint32_t variable that will hold the
///   number of blocks on the SD card to read or write.
///
/// @return Returns 0 on success, EINVAL on failure.
int sdCardGetReadWriteParameters(
  SdCardState *sdCardState, SdCommandParams *sdCommandParams,
  uint32_t *startSdBlock, uint32_t *numSdBlocks
) {
  *startSdBlock = sdCommandParams->startBlock
    << sdCardState->bsDevice->blockBitShift;
  *numSdBlocks = sdCommandParams->numBlocks
    << sdCardState->bsDevice->blockBitShift;
  if ((*startSdBlock + *numSdBlocks) > sdCardState->numBlocks) {
    printString(__func__);
    printString(": ERROR! Invalid R/W range\n");
    return EINVAL;
  }

  return 0;
}

/// @fn int sdReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Read a specified number of blocks of a given size from the SD card
/// into a provided buffer.
///
/// @param context The task ID of the SD card task to read from, cast to
///   a void*.
/// @param startBlock The start block to read from in terms of the caller's
///   context.
/// @param numBlocks The number of blocks to read in terms of the caller's
///   context.
/// @param blockSize The size of the blocks as known to the caller.
/// @param buffer A pointer to the byte buffer to read the data into.
///
/// @return Returns 0 on success, POSIX error code on failure.
int sdReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  intptr_t sdCardTask = (intptr_t) context;
  SdCommandParams sdCommandParams;
  sdCommandParams.startBlock = startBlock;
  sdCommandParams.numBlocks = numBlocks;
  sdCommandParams.blockSize = blockSize;
  sdCommandParams.buffer = buffer;

  TaskMessage *taskMessage = initSendTaskMessageToTaskId(
    sdCardTask, SD_CARD_READ_BLOCKS,
    /* data= */ &sdCommandParams, sizeof(sdCommandParams), true);
  taskMessageWaitForDone(taskMessage, NULL);
  int returnValue = (int) ((intptr_t) taskMessageData(taskMessage));
  taskMessageRelease(taskMessage);

  return returnValue;
}

/// @fn int sdWriteBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Write a specified number of blocks of a given size to the SD card
/// from a provided buffer.
///
/// @param context The task ID of the SD card task to write to, cast to
///   a void*.
/// @param startBlock The start block to write to in terms of the caller's
///   context.
/// @param numBlocks The number of blocks to write out terms of the caller's
///   context.
/// @param blockSize The size of the blocks as known to the caller.
/// @param buffer A pointer to the byte buffer to write the data from.
///
/// @return Returns 0 on success, POSIX error code on failure.
int sdWriteBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  intptr_t sdCardTask = (intptr_t) context;
  SdCommandParams sdCommandParams;
  sdCommandParams.startBlock = startBlock;
  sdCommandParams.numBlocks = numBlocks;
  sdCommandParams.blockSize = blockSize;
  sdCommandParams.buffer = (uint8_t*) buffer;

  TaskMessage *taskMessage = initSendTaskMessageToTaskId(
    sdCardTask, SD_CARD_WRITE_BLOCKS,
    /* data= */ &sdCommandParams, /* size= */ sizeof(sdCommandParams), true);
  taskMessageWaitForDone(taskMessage, NULL);
  int returnValue = (int) ((intptr_t) taskMessageData(taskMessage));
  taskMessageRelease(taskMessage);

  return returnValue;
}

/// @fn int schedSdReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Scheduler-specific implementation to read a specified number of
/// blocks of a given size from the SD card into a provided buffer.
///
/// @param context The task ID of the SD card task to read from, cast to
///   a void*.
/// @param startBlock The start block to read from in terms of the caller's
///   context.
/// @param numBlocks The number of blocks to read in terms of the caller's
///   context.
/// @param blockSize The size of the blocks as known to the caller.
/// @param buffer A pointer to the byte buffer to read the data into.
///
/// @return Returns 0 on success, negative POSIX error code on failure.
int schedSdReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  intptr_t sdCardTask = (intptr_t) context;
  SdCommandParams sdCommandParams;
  sdCommandParams.startBlock = startBlock;
  sdCommandParams.numBlocks = numBlocks;
  sdCommandParams.blockSize = blockSize;
  sdCommandParams.buffer = buffer;

  TaskQueue *currentReady = SCHEDULER_STATE->currentReady;
  SCHEDULER_STATE->currentReady
    = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

  TaskMessage *taskMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (taskMessage == NULL);
    ii++
  ) {
    SCHEDULER_STATE->runScheduler();
    taskMessage = getAvailableMessage();
  }
  if (taskMessage == NULL) {
    printInt(getRunningTaskId());
    printString(": ");
    printString(__func__);
    printString(": ERROR: Out of task messages\n");
    return -ENOMEM;
  }

  taskMessageInit(taskMessage, SD_CARD_READ_BLOCKS,
    &sdCommandParams, sizeof(sdCommandParams), true);
  if (sendTaskMessageToTaskId(sdCardTask, taskMessage) != taskSuccess) {
    return -ENXIO;
  }

  while (taskMessageDone(taskMessage) == false) {
    SCHEDULER_STATE->runScheduler();
  }
  int returnValue = (int) ((intptr_t) taskMessageData(taskMessage));
  taskMessageRelease(taskMessage);

  SCHEDULER_STATE->currentReady = currentReady;
  return returnValue;
}

/// @fn int schedSdWriteBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Scheduler-specific implementation to write a specified number of
/// blocks of a given size to the SD card from a provided buffer.
///
/// @param context The task ID of the SD card task to write to, cast to
///   a void*.
/// @param startBlock The start block to write to in terms of the caller's
///   context.
/// @param numBlocks The number of blocks to write out terms of the caller's
///   context.
/// @param blockSize The size of the blocks as known to the caller.
/// @param buffer A pointer to the byte buffer to write the data from.
///
/// @return Returns 0 on success, negative POSIX error code on failure.
int schedSdWriteBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  intptr_t sdCardTask = (intptr_t) context;
  SdCommandParams sdCommandParams;
  sdCommandParams.startBlock = startBlock;
  sdCommandParams.numBlocks = numBlocks;
  sdCommandParams.blockSize = blockSize;
  sdCommandParams.buffer = (uint8_t*) buffer;

  TaskQueue *currentReady = SCHEDULER_STATE->currentReady;
  SCHEDULER_STATE->currentReady
    = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

  TaskMessage *taskMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (taskMessage == NULL);
    ii++
  ) {
    SCHEDULER_STATE->runScheduler();
    taskMessage = getAvailableMessage();
  }
  if (taskMessage == NULL) {
    printInt(getRunningTaskId());
    printString(": ");
    printString(__func__);
    printString(": ERROR: Out of task messages\n");
    return -ENOMEM;
  }

  taskMessageInit(taskMessage, SD_CARD_WRITE_BLOCKS,
    &sdCommandParams, sizeof(sdCommandParams), true);
  if (sendTaskMessageToTaskId(sdCardTask, taskMessage) != taskSuccess) {
    return -ENXIO;
  }

  while (taskMessageDone(taskMessage) == false) {
    SCHEDULER_STATE->runScheduler();
  }
  int returnValue = nanoOsMessageDataValue(taskMessage, int);
  taskMessageRelease(taskMessage);

  SCHEDULER_STATE->currentReady = currentReady;
  return returnValue;
}

