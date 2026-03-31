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

// NanoOs includes
#include "NanoOsLibC.h"
#include "Tasks.h"

// Simulator includes
#include "SdCardPosix.h"

// Must come last
#include "../user/NanoOsStdio.h"

//// #define printDebug(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define printDebug(fmt, ...) {}

// Implementation prototypes.
const char* sdCardInit(SdCardState *sdCardState,
  BlockStorageDevice *sdDevice, const char *sdCardDevicePath);
int sdCardRead(int devFd, void *buffer, size_t start, size_t len);
int sdCardWrite(int devFd, const void *buffer, size_t start, size_t len);

/// @fn int sdCardReadBlocksCommandHandler(
///   SdCardState *sdCardState, TaskMessage *taskMessage)
///
/// @brief Command handler for the SD_CARD_READ_BLOCKS command.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   SD card task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the SD card task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int sdCardReadBlocksCommandHandler(
  SdCardState *sdCardState, TaskMessage *taskMessage
) {
  printDebug("sdCardReadBlocksCommandHandler: Enter\n");
  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  printDebug("sdCardReadBlocksCommandHandler: Got NanoOsMessage\n");
  
  int devFd = (int) ((intptr_t) sdCardState->context);
  if (devFd < 0) {
    // Nothing we can do.
    printDebug("sdCardReadBlocksCommandHandler: Invalid file descriptor\n");
    nanoOsMessage->data = EIO;
    taskMessageSetDone(taskMessage);
    printDebug("sdCardReadBlocksCommandHandler: Returning early\n");
    return 0;
  }
  printDebug("sdCardReadBlocksCommandHandler: context is *NOT* NULL\n");
  
  SdCommandParams *sdCommandParams
    = nanoOsMessageDataPointer(taskMessage, SdCommandParams*);
  printDebug("sdCardReadBlocksCommandHandler: Got sdCommandParams\n");
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);
  printDebug("sdCardReadBlocksCommandHandler: Got read/write parameters\n");

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    returnValue = sdCardRead(devFd, buffer,
      sdCardState->blockSize * startSdBlock,
      sdCardState->blockSize * numSdBlocks);
  }

  printDebug("sdCardReadBlocksCommandHandler: Exiting\n");
  nanoOsMessage->data = returnValue;
  printDebug("sdCardReadBlocksCommandHandler: Setting message to done\n");
  taskMessageSetDone(taskMessage);

  printDebug("sdCardReadBlocksCommandHandler: Returning\n");
  return 0;
}

/// @fn int sdCardWriteBlocksCommandHandler(
///   SdCardState *sdCardState, TaskMessage *taskMessage)
///
/// @brief Command handler for the SD_CARD_WRITE_BLOCKS command.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   SD card task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the SD card task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int sdCardWriteBlocksCommandHandler(
  SdCardState *sdCardState, TaskMessage *taskMessage
) {
  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  
  int devFd = (int) ((intptr_t) sdCardState->context);
  if (devFd < 0) {
    // Nothing we can do.
    nanoOsMessage->data = EIO;
    taskMessageSetDone(taskMessage);
    return 0;
  }
  
  SdCommandParams *sdCommandParams
    = nanoOsMessageDataPointer(taskMessage, SdCommandParams*);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    returnValue = sdCardWrite(devFd, buffer,
      sdCardState->blockSize * startSdBlock,
      sdCardState->blockSize * numSdBlocks);
  }

  nanoOsMessage->data = returnValue;
  taskMessageSetDone(taskMessage);

  return 0;
}

/// @var sdCardCommandHandlers
///
/// @brief Array of SdCardCommandHandler function pointers to handle commands
/// received by the runSdCard function.
SdCardCommandHandler sdCardCommandHandlers[] = {
  sdCardReadBlocksCommandHandler,         // SD_CARD_READ_BLOCKS
  sdCardWriteBlocksCommandHandler,        // SD_CARD_WRITE_BLOCKS
};

/// @fn void handleSdCardMessages(SdCardState *sdCardState)
///
/// @brief Handle sdCard messages from the task's queue until there are no
/// more waiting.
///
/// @param sdCardState A pointer to the SdCardState structure maintained by the
///   sdCard task.
///
/// @return This function returns no value.
void handleSdCardMessages(SdCardState *sdCardState) {
  TaskMessage *taskMessage = taskMessageQueuePop();
  while (taskMessage != NULL) {
    SdCardCommandResponse messageType
      = (SdCardCommandResponse) taskMessageType(taskMessage);
    if (messageType >= NUM_SD_CARD_COMMANDS) {
      printDebug("handleSdCardMessages: Received invalid messageType %d\n",
        messageType);
      taskMessage = taskMessageQueuePop();
      continue;
    }
    
    sdCardCommandHandlers[messageType](sdCardState, taskMessage);
    taskMessage = taskMessageQueuePop();
  }
  
  return;
}

/// @fn void* runSdCardPosix(void *args)
///
/// @brief Task entry-point for the SD card task.  Sets up and
/// configures access to the SD card reader and then enters an infinite loop
/// for processing commands.
///
/// @param args Any arguments to this function, cast to a void*.  Currently
///   ignored by this function.
///
/// @return This function never returns, but would return NULL if it did.
void* runSdCardPosix(void *args) {
  const char *sdCardDevicePath = (char*) args;

  SdCardState sdCardState;
  memset(&sdCardState, 0, sizeof(sdCardState));
  BlockStorageDevice sdDevice = {
    .context = (void*) ((intptr_t) getRunningTaskId()),
    .readBlocks = sdReadBlocks,
    .writeBlocks = sdWriteBlocks,
    .blockSize = 512,
    .blockBitShift = 0,
    .partitionNumber = 0,
  };
  const char *openError = sdCardInit(&sdCardState, &sdDevice, sdCardDevicePath);

  taskYieldValue(&sdDevice);
  if (((intptr_t) sdCardState.context) < 0) {
    fprintf(stderr, "ERROR: Failed to open sdCardDevicePath \"%s\"\n",
      sdCardDevicePath);
    fprintf(stderr, "Error returned: %s\n", openError);
  }

  TaskMessage *schedulerMessage = NULL;
  while (1) {
    schedulerMessage = (TaskMessage*) taskYield();
    if (schedulerMessage != NULL) {
      // We have a message from the scheduler that we need to task.  This
      // is not the expected case, but it's the priority case, so we need to
      // list it first.
      SdCardCommandResponse messageType
        = (SdCardCommandResponse) taskMessageType(schedulerMessage);
      if (messageType < NUM_SD_CARD_COMMANDS) {
        sdCardCommandHandlers[messageType](&sdCardState, schedulerMessage);
      } else {
        fprintf(stderr,
          "ERROR: Received unknown sdCard command %d from scheduler.\n",
          messageType);
      }
    } else {
      handleSdCardMessages(&sdCardState);
    }
  }

  return NULL;
}

