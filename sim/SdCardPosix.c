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
#include "Processes.h"

// Simulator includes
#include "SdCardPosix.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Implementation prototypes.
const char* sdCardInit(SdCardState *sdCardState,
  BlockStorageDevice *sdDevice, const char *sdCardDevicePath);
int sdCardRead(int devFd, void *buffer, size_t start, size_t len);
int sdCardWrite(int devFd, const void *buffer, size_t start, size_t len);

/// @fn int sdCardPosixReadBlocksCommandHandler(
///   SdCardState *sdCardState, ProcessMessage *processMessage)
///
/// @brief Command handler for the SD_CARD_READ_BLOCKS command.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   SD card process.
/// @param processMessage A pointer to the ProcessMessage that was received by
///   the SD card process.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int sdCardPosixReadBlocksCommandHandler(
  SdCardState *sdCardState, ProcessMessage *processMessage
) {
  printDebugString("sdCardPosixReadBlocksCommandHandler: Enter\n");
  printDebugString("sdCardPosixReadBlocksCommandHandler: Got ProcessMessage\n");
  
  int devFd = (int) ((intptr_t) sdCardState->context);
  if (devFd < 0) {
    // Nothing we can do.
    printDebugString(
      "sdCardPosixReadBlocksCommandHandler: Invalid file descriptor\n");
    processMessageData(processMessage) = (void*) ((intptr_t) EIO);
    processMessageSetDone(processMessage);
    printDebugString("sdCardPosixReadBlocksCommandHandler: Returning early\n");
    return 0;
  }
  printDebugString("sdCardPosixReadBlocksCommandHandler: context is *NOT* NULL\n");
  
  SdCommandParams *sdCommandParams
    = (SdCommandParams*) processMessageData(processMessage);
  printDebugString("sdCardPosixReadBlocksCommandHandler: Got sdCommandParams\n");
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);
  printDebugString(
    "sdCardPosixReadBlocksCommandHandler: Got read/write parameters\n");

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    printDebugString("Issuing sdCardRead\n");
    returnValue = sdCardRead(devFd, buffer,
      sdCardState->blockSize * startSdBlock,
      sdCardState->blockSize * numSdBlocks);
  }

  printDebugString("sdCardPosixReadBlocksCommandHandler: Exiting\n");
  processMessageData(processMessage) = (void*) ((intptr_t) returnValue);
  printDebugString("sdCardPosixReadBlocksCommandHandler: Setting message to done\n");
  processMessageSetDone(processMessage);

  printDebugString("sdCardPosixReadBlocksCommandHandler: Returning\n");
  return 0;
}

/// @fn int sdCardPosixWriteBlocksCommandHandler(
///   SdCardState *sdCardState, ProcessMessage *processMessage)
///
/// @brief Command handler for the SD_CARD_WRITE_BLOCKS command.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   SD card process.
/// @param processMessage A pointer to the ProcessMessage that was received by
///   the SD card process.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int sdCardPosixWriteBlocksCommandHandler(
  SdCardState *sdCardState, ProcessMessage *processMessage
) {
  int devFd = (int) ((intptr_t) sdCardState->context);
  if (devFd < 0) {
    // Nothing we can do.
    processMessageData(processMessage) = (void*) ((intptr_t) EIO);
    processMessageSetDone(processMessage);
    return 0;
  }
  
  SdCommandParams *sdCommandParams
    = (SdCommandParams*) processMessageData(processMessage);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    returnValue = sdCardWrite(devFd, buffer,
      sdCardState->blockSize * startSdBlock,
      sdCardState->blockSize * numSdBlocks);
  }

  processMessageData(processMessage) = (void*) ((intptr_t) returnValue);
  processMessageSetDone(processMessage);

  return 0;
}

/// @var sdCardPosixCommandHandlers
///
/// @brief Array of SdCardCommandHandler function pointers to handle commands
/// received by the runSdCard function.
SdCardCommandHandler sdCardPosixCommandHandlers[] = {
  sdCardPosixReadBlocksCommandHandler,         // SD_CARD_READ_BLOCKS
  sdCardPosixWriteBlocksCommandHandler,        // SD_CARD_WRITE_BLOCKS
};

/// @fn void handleSdCardPosixMessages(SdCardState *sdCardState)
///
/// @brief Handle sdCard messages from the process's queue until there are no
/// more waiting.
///
/// @param sdCardState A pointer to the SdCardState structure maintained by the
///   sdCard process.
///
/// @return This function returns no value.
void handleSdCardPosixMessages(SdCardState *sdCardState) {
  ProcessMessage *processMessage = processMessageQueuePop();
  while (processMessage != NULL) {
    SdCardCommandResponse messageType
      = (SdCardCommandResponse) processMessageType(processMessage);
    if (messageType >= NUM_SD_CARD_COMMANDS) {
      printDebugString("handleSdCardPosixMessages: Received invalid messageType ");
      printDebugInt(messageType);
      printDebugString("\n");
      processMessage = processMessageQueuePop();
      continue;
    }
    
    sdCardPosixCommandHandlers[messageType](sdCardState, processMessage);
    processMessage = processMessageQueuePop();
  }
  
  return;
}

/// @fn void* runSdCardPosix(void *args)
///
/// @brief Process entry-point for the SD card process.  Sets up and
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
    .context = (void*) ((intptr_t) getRunningPid()),
    .readBlocks = sdReadBlocks,
    .writeBlocks = sdWriteBlocks,
    .schedReadBlocks = schedSdReadBlocks,
    .schedWriteBlocks = schedSdWriteBlocks,
    .blockSize = 512,
    .blockBitShift = 0,
    .partitionNumber = 0,
  };
  const char *openError = sdCardInit(&sdCardState, &sdDevice, sdCardDevicePath);

  processYieldValue(&sdDevice);
  if (((intptr_t) sdCardState.context) < 0) {
    fprintf(stderr, "ERROR: Failed to open sdCardDevicePath \"%s\"\n",
      sdCardDevicePath);
    fprintf(stderr, "Error returned: %s\n", openError);
  }

  ProcessMessage *schedulerMessage = NULL;
  while (1) {
    schedulerMessage = (ProcessMessage*) processYield();
    if (schedulerMessage != NULL) {
      // We have a message from the scheduler that we need to process.  This
      // is not the expected case, but it's the priority case, so we need to
      // list it first.
      SdCardCommandResponse messageType
        = (SdCardCommandResponse) processMessageType(schedulerMessage);
      if (messageType < NUM_SD_CARD_COMMANDS) {
        sdCardPosixCommandHandlers[messageType](&sdCardState, schedulerMessage);
      } else {
        fprintf(stderr,
          "ERROR: Received unknown sdCard command %d from scheduler.\n",
          messageType);
      }
    } else {
      handleSdCardPosixMessages(&sdCardState);
    }
  }

  return NULL;
}

