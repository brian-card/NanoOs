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

/// @file SdCardSpi.c
///
/// @brief SPI implementation of the SD card logic.
///
/// @note  We do *NOT* need to check HAL->spiHal for NULL in this
/// implementation.  If it was NULL, the SD card device would never have been
/// started with this implementation.

// Custom includes
#include "SdCardSpi.h"
#include "Hal.h"
#include "NanoOs.h"
#include "Tasks.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

// SD card commands
#define CMD0    0x40  // GO_IDLE_STATE
#define CMD8    0x48  // SEND_IF_COND
#define CMD9    0x49  // SEND_CSD
#define CMD16   0x50  // SET_BLOCKLEN
#define CMD17   0x51  // READ_SINGLE_BLOCK
#define CMD24   0x58  // WRITE_BLOCK
#define CMD58   0x7A  // READ_OCR
#define CMD55   0x77  // APP_CMD
#define ACMD41  0x69  // SD_SEND_OP_COND

// R1 Response bit flags
#define R1_IDLE_STATE  0x01
#define R1_ERASE_RESET 0x02
#define R1_ILLEGAL_CMD 0x04
#define R1_CRC_ERROR   0x08
#define R1_ERASE_SEQ   0x10
#define R1_ADDR_ERROR  0x20
#define R1_PARAM_ERROR 0x40

/// @def SD_CARD_SPI_DEVICE
///
/// @brief The SPI device ID to use in SPI calls in the HAL.
#define SD_CARD_SPI_DEVICE 0

/// @fn uint8_t sdSpiSendCommand(int sdCardSpiDevice, uint8_t cmd, uint32_t arg)
///
/// @brief Send a command and its argument to the SD card over the SPI
/// interface.
///
/// @brief sdCardSpiDevice The zero-based SPI device ID to use.
/// @param cmd The 8-bit SD command to send to the SD card.
/// @param arg The 32-bit arguent to send for the SD command.
///
/// @return Returns the 8-bit command response from the SD card.
uint8_t sdSpiSendCommand(int sdCardSpiDevice, uint8_t cmd, uint32_t arg) {
  HAL->spiHal->startSpiTransfer(sdCardSpiDevice);
  
  // Command byte
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, cmd | 0x40);
  
  // Argument
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, (arg >> 24) & 0xff);
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, (arg >> 16) & 0xff);
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, (arg >>  8) & 0xff);
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, (arg >>  0) & 0xff);
  
  // CRC - only needed for CMD0 and CMD8
  uint8_t crc = 0xFF;
  if (cmd == CMD0) {
    crc = 0x95; // Valid CRC for CMD0
  } else if (cmd == CMD8) {
    crc = 0x87; // Valid CRC for CMD8 (0x1AA)
  }
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, crc);
  
  // Wait for response
  uint8_t response;
  for (int ii = 0; ii < 10; ii++) {
    response = HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
    if ((response & 0x80) == 0) {
      break; // Exit if valid response
    }
  }
  
  return response;
}

/// @fn int sdSpiCardInit(SdCardSpiArgs *sdCardSpiArgs)
///
/// @brief Initialize the SD card for communication with the OS.
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the information needed to initialize the card.
///
/// @return Returns the version of the connected card on success (1 or 2),
/// 0 on error.
int sdSpiCardInit(SdCardSpiArgs *sdCardSpiArgs) {
  uint8_t response;
  uint16_t timeoutCount;
  bool isSDv2 = false;
  
  // Set up SPI at the default speed
  int initStatus = HAL->spiHal->initSpiDevice(SD_CARD_SPI_DEVICE,
    sdCardSpiArgs->spiCsDio,
    sdCardSpiArgs->spiSckDio,
    sdCardSpiArgs->spiCopiDio,
    sdCardSpiArgs->spiCipoDio,
    8000000
  );
  if (initStatus != 0) {
    // Just pass the error upward.
    return initStatus;
  }
  
  // Extended power up sequence - Send more clock cycles
  for (int ii = 0; ii < 128; ii++) {
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
  }
  
  // Send CMD0 to enter SPI mode
  timeoutCount = 200;  // Extended timeout
  do {
    for (int ii = 0; ii < 8; ii++) {  // More dummy clocks
      HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD0, 0);
    if (--timeoutCount == 0) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      printString("ERROR! CMD0 timed out\n");
      return -ETIMEDOUT;
    }
  } while (response != R1_IDLE_STATE);
  
  // Send CMD8 to check version
  for (int ii = 0; ii < 8; ii++) {
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
  }
  response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD8, 0x000001AA);
  if (response == R1_IDLE_STATE) {
    isSDv2 = true;
    for (int ii = 0; ii < 4; ii++) {
      response = HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
  }
  HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
  
  // Initialize card with ACMD41
  timeoutCount = 20000;  // Much longer timeout
  do {
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD55, 0);
    HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
    
    for (int ii = 0; ii < 8; ii++) {
      HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    
    // Try both with and without HCS bit based on card version
    uint32_t acmd41Arg = isSDv2 ? 0x40000000 : 0;
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, ACMD41, acmd41Arg);
    HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
    
    if (--timeoutCount == 0) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      printString("ERROR! ACMD41 timed out\n");
      return -ETIMEDOUT;
    }
  } while (response != 0);
  
  // If we get here, card is initialized
  for (int ii = 0; ii < 8; ii++) {
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
  }
  
  HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
  return isSDv2 ? 2 : 1;
}

/// @fn bool sdSpiReadBlocks(SdCardState *sdCardState,
///   uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer)
///
/// @brief Read blocks from an SD card into a buffer.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   runSdCard task.
/// @param startBlock The logical block number on the SD card to start from.
/// @param numBlocks The number of blocks to read from the device.
/// @param buffer A pointer to a character buffer to read the blocks into.
///
/// @return Returns 0 on success, error code on failure.
int sdSpiReadBlocks(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer
) {
  // Check that buffer is not null
  if (buffer == NULL) {
    return EINVAL;
  }
  
  for (uint32_t ii = 0; ii < numBlocks; ii++) {
    uint32_t address = startBlock;
    if (sdCardState->sdCardVersion == 1) {
      address *= sdCardState->blockSize; // Convert to byte address
    }
    
    // Send READ_SINGLE_BLOCK command
    uint8_t response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD17, address);
    if (response != 0x00) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Command failed
    }
    
    // Wait for data token (0xFE)
    uint16_t timeout = 10000;
    while (timeout--) {
      response = HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
      if (response == 0xFE) {
        break;
      }
      if (timeout == 0) {
        HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
        return EIO;  // Timeout waiting for data
      }
    }
    
    // Read the block
    memset(buffer, 0xFF, sdCardState->blockSize);
    if (HAL->spiHal->spiTransferBytes(
      SD_CARD_SPI_DEVICE, buffer, sdCardState->blockSize) != 0
    ) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Command failed
    }
    
    // Read CRC (2 bytes, ignored)
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    
    buffer += sdCardState->blockSize;
    startBlock++;
  }
  
  HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
  return 0;
}

/// @fn int sdSpiWriteBlocks(SdCardState *sdCardState,
///   uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer)
/// 
/// @brief Write a buffer of blocks to an SD card.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   runSdCard task.
/// @param startBlock The logical block number to start the write at.
/// @param numBlocks The number of blocks to write.
/// @param buffer A pointer to a character buffer to write the blocks from.
///   NOTE: The contents of this buffer may be modified by this function and
///   are undefined after it completes irrespective of whether or not this
///   function succeeds.
///
/// @return Returns 0 on success, error code on failure.
int sdSpiWriteBlocks(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer
) {
  if (buffer == NULL) {
    return EINVAL;
  }
  
  // Check if card is responsive
  HAL->spiHal->startSpiTransfer(SD_CARD_SPI_DEVICE);
  uint8_t response = HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
  if (response != 0xFF) {
    HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
    return EIO;
  }
  
  for (uint32_t ii = 0; ii < numBlocks; ii++) {
    uint32_t address = startBlock;
    if (sdCardState->sdCardVersion == 1) {
      address *= sdCardState->blockSize; // Convert to byte address
    }
    
    // Send WRITE_BLOCK command
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD24, address);
    if (response != 0x00) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Command failed
    }
    
    // Wait for card to be ready before sending data
    uint16_t timeout = 10000;
    do {
      response = HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
      if (--timeout == 0) {
        HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
        return EIO;
      }
    } while (response != 0xFF);
    
    // Send start token
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFE);
    
    // Write data
    if (HAL->spiHal->spiTransferBytes(
      SD_CARD_SPI_DEVICE, buffer, sdCardState->blockSize) != 0
    ) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Bad response
    }
    
    // Send dummy CRC
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    
    // Get data response
    response = HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF);
    if ((response & 0x1F) != 0x05) {
      HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Bad response
    }
    
    // Wait for write to complete
    timeout = 10000;
    while (timeout--) {
      if (HAL->spiHal->spiTransfer8(SD_CARD_SPI_DEVICE, 0xFF) != 0x00) {
        break;
      }
      if (timeout == 0) {
        HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
        return EIO; // Write timeout
      }
    }
    
    buffer += sdCardState->blockSize;
    startBlock++;
  }
  
  HAL->spiHal->endSpiTransfer(SD_CARD_SPI_DEVICE);
  return 0;
}

/// @fn int16_t sdSpiGetBlockSize(int sdCardSpiDevice)
///
/// @brief Get the size, in bytes, of blocks on the SD card as presented to the
/// host.
///
/// @param sdCardSpiDevice The zero-based ID of the SPI device.
///
/// @return Returns the number of bytes per block on success, negative error
/// code on failure.
int16_t sdSpiGetBlockSize(int sdCardSpiDevice) {
  uint8_t response = sdSpiSendCommand(sdCardSpiDevice, CMD9, 0);
  if (response != 0x00) {
    HAL->spiHal->endSpiTransfer(sdCardSpiDevice);
    printString(__func__);
    printString(": ERROR! CMD9 returned ");
    printInt(response);
    printString("\n");
    return -1;
  }

  for(int i = 0; i < 100; i++) {
    response = HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
    if (response == 0xFE) {
      break;  // Data token
    }
  }
  
  // Read 16-byte CSD register
  uint8_t csd[16];
  for(int i = 0; i < 16; i++) {
    csd[i] = HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
  }
  
  // Read 2 CRC bytes
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
  HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
  HAL->spiHal->endSpiTransfer(sdCardSpiDevice);

  // For CSD Version 1.0 and 2.0, READ_BL_LEN is at the same location
  uint8_t readBlockLength = (csd[5] & 0x0F);
  return (int16_t) (((uint16_t) 1) << readBlockLength);
}

/// @fn int sdSpiGetBlockCount(sdCardSpiDevice)
///
/// @brief Get the total number of available blocks on an SD card.
///
/// @param sdCardSpiDevice The zero-based ID of the SPI device.
///
/// @return Returns the number of blocks available on success, negative error
/// code on failure.
int32_t sdSpiGetBlockCount(int sdCardSpiDevice) {
  uint8_t cardSpecificData[16];
  uint32_t blockCount = 0;
  
  // Send SEND_CSD command
  uint8_t response = sdSpiSendCommand(sdCardSpiDevice, CMD9, 0);
  if (response != 0x00) {
    HAL->spiHal->endSpiTransfer(sdCardSpiDevice);
    printString(__func__);
    printString(": ERROR! CMD9 returned ");
    printInt(response);
    printString("\n");
    return -1;
  }
  
  // Wait for data token
  uint16_t timeoutCount = 10000;
  while (timeoutCount--) {
    response = HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
    if (response == 0xFE) {
      break;
    }
    if (timeoutCount == 0) {
      HAL->spiHal->endSpiTransfer(sdCardSpiDevice);
      return -2;
    }
  }
  
  // Read CSD register
  for (int ii = 0; ii < 16; ii++) {
    cardSpecificData[ii] = HAL->spiHal->spiTransfer8(sdCardSpiDevice, 0xFF);
  }
  
  HAL->spiHal->endSpiTransfer(sdCardSpiDevice);
  
  // Calculate capacity based on CSD version
  if ((cardSpecificData[0] >> 6) == 0x01) {  // CSD version 2.0
    // C_SIZE is bits [69:48] in CSD
    uint32_t capacity = ((uint32_t) cardSpecificData[7] & 0x3F) << 16;
    capacity |= (uint32_t) cardSpecificData[8] << 8;
    capacity |= (uint32_t) cardSpecificData[9];
    blockCount = (capacity + 1) << 10; // Multiply by 1024 blocks
  } else {  // CSD version 1.0
    // Calculate from C_SIZE, C_SIZE_MULT, and READ_BL_LEN
    uint32_t capacity = ((uint32_t) (cardSpecificData[6] & 0x03) << 10);
    capacity |= (uint32_t) cardSpecificData[7] << 2;
    capacity |= (uint32_t) (cardSpecificData[8] >> 6);
    
    uint8_t capacityMultiplier = ((cardSpecificData[9] & 0x03) << 1);
    capacityMultiplier |= ((cardSpecificData[10] & 0x80) >> 7);
    
    uint8_t readBlockLength = cardSpecificData[5] & 0x0F;
    
    blockCount = (capacity + 1) << (capacityMultiplier + 2);
    blockCount <<= (readBlockLength - 9);  // Adjust for 512-byte blocks
  }
  
  return (int32_t) blockCount;
}

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
  SdCommandParams *sdCommandParams
    = nanoOsMessageDataPointer(taskMessage, SdCommandParams*);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    returnValue = sdSpiReadBlocks(sdCardState,
      startSdBlock, numSdBlocks, buffer);
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = returnValue;
  taskMessageSetDone(taskMessage);

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
  SdCommandParams *sdCommandParams
    = nanoOsMessageDataPointer(taskMessage, SdCommandParams*);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteParameters(
    sdCardState, sdCommandParams, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandParams->buffer;
    returnValue = sdSpiWriteBlocks(sdCardState,
      startSdBlock, numSdBlocks, buffer);
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
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
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": Invalid message type");
      printInt(messageType);
      printString("\n");

      taskMessage = taskMessageQueuePop();
      continue;
    }
    
    sdCardCommandHandlers[messageType](sdCardState, taskMessage);
    taskMessage = taskMessageQueuePop();
  }
  
  return;
}

/// @fn void* runSdCardSpi(void *args)
///
/// @brief Task entry-point for the SD card task.  Sets up and
/// configures access to the SD card reader and then enters an infinite loop
/// for processing commands.
///
/// @param args Any arguments to this function, cast to a void*.  Currently
///   ignored by this function.
///
/// @return This function never returns, but would return NULL if it did.
void* runSdCardSpi(void *args) {
  SdCardSpiArgs *sdCardSpiArgs = (SdCardSpiArgs*) args;

  SdCardState sdCardState;
  memset(&sdCardState, 0, sizeof(sdCardState));
  BlockStorageDevice blockStorageDevice = {
    .context = (void*) ((intptr_t) getRunningTaskId()),
    .readBlocks = sdReadBlocks,
    .writeBlocks = sdWriteBlocks,
    .schedReadBlocks = schedSdReadBlocks,
    .schedWriteBlocks = schedSdWriteBlocks,
    .blockSize = 0,
    .blockBitShift = 0,
    .partitionNumber = 0,
  };
  sdCardState.bsDevice = &blockStorageDevice;

  sdCardState.sdCardVersion = sdSpiCardInit(sdCardSpiArgs);
  if (sdCardState.sdCardVersion > 0) {
    sdCardState.blockSize = blockStorageDevice.blockSize
      = sdSpiGetBlockSize(SD_CARD_SPI_DEVICE);
    sdCardState.numBlocks = sdSpiGetBlockCount(SD_CARD_SPI_DEVICE);
#ifdef SD_CARD_DEBUG
    printString("Card is ");
    printString((sdCardState.sdCardVersion == 1) ? "SDSC" : "SDHC/SDXC");
    printString("\n");

    printString("Card block size = ");
    printInt(blockStorageDevice.blockSize);
    printString("\n");
    printLong(sdCardState.numBlocks);
    printString(" total blocks (");
    printLongLong(((int64_t) sdCardState.numBlocks)
      * ((int64_t) sdCardState.blockSize));
    printString(" total bytes)\n");
#endif // SD_CARD_DEBUG
  } else {
    printString("ERROR! sdSpiCardInit returned status: ");
    printString(strerror(-sdCardState.sdCardVersion));
    printString("\n");
  }
  taskYieldValue(&blockStorageDevice);

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
        printString("ERROR: Received unknown sdCard command ");
        printInt(messageType);
        printString(" from scheduler.\n");
      }
    } else {
      handleSdCardMessages(&sdCardState);
    }
  }

  return NULL;
}

