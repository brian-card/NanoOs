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

// Custom includes
#include "SdCardSpi.h"
#include "Hal.h"
#include "Logger.h"
#include "NanoOs.h"
#include "Processes.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

// SD card commands
#define CMD0    0x40  // GO_IDLE_STATE
#define CMD8    0x48  // SEND_IF_COND
#define CMD9    0x49  // SEND_CSD
#define CMD16   0x50  // SET_BLOCKLEN
#define CMD12   0x4C  // STOP_TRANSMISSION
#define CMD17   0x51  // READ_SINGLE_BLOCK
#define CMD18   0x52  // READ_MULTIPLE_BLOCK
#define CMD24   0x58  // WRITE_BLOCK
#define CMD25   0x59  // WRITE_MULTIPLE_BLOCK
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

/// @def SD_SPI_INIT_BAUD
///
/// @brief Bus clock for the card-identification phase.  The SD physical spec
/// requires CMD0 through ACMD41 to run at 100-400 kHz; the HAL clamps this to
/// whatever its hardware can produce at or below the request.
#define SD_SPI_INIT_BAUD 400000

/// @def SD_SPI_FAST_BAUD_MAX
///
/// @brief Ceiling for the data-phase bus clock.  sdSpiNegotiateFastBaud()
/// starts here and halves the request until a CMD9 (SEND_CSD) block read comes
/// back with a valid CRC-16, or the next halving would drop below
/// SD_SPI_INIT_BAUD - a rate the identification phase already proved works.
/// Each HAL still clamps the request to what its SPI peripheral can produce.
#define SD_SPI_FAST_BAUD_MAX 8000000

/// @var _sdSpiFastBaud
///
/// @brief The data-phase bus clock actually in use.  Seeded from
/// SD_SPI_FAST_BAUD_MAX and lowered by sdSpiNegotiateFastBaud() to the fastest
/// rate this board / card / wiring combination reads without corruption.  This
/// is deliberately a runtime variable rather than a constant: the safe ceiling
/// is a property of the hardware, discovered by probing, and a card-process
/// restart re-discovers it.
static uint32_t _sdSpiFastBaud = SD_SPI_FAST_BAUD_MAX;

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
  HAL->spi.startTransfer(sdCardSpiDevice);

  // At least 8 clocks (Ncs) with the chip select asserted before the command
  // byte, per the SD physical spec.  For a mid-stream call - CS already low, a
  // transfer already active - this is just a harmless inter-command gap byte.
  HAL->spi.transfer8(sdCardSpiDevice, 0xFF);

  // Command byte
  HAL->spi.transfer8(sdCardSpiDevice, cmd | 0x40);
  
  // Argument
  HAL->spi.transfer8(sdCardSpiDevice, (arg >> 24) & 0xff);
  HAL->spi.transfer8(sdCardSpiDevice, (arg >> 16) & 0xff);
  HAL->spi.transfer8(sdCardSpiDevice, (arg >>  8) & 0xff);
  HAL->spi.transfer8(sdCardSpiDevice, (arg >>  0) & 0xff);
  
  // CRC - only needed for CMD0 and CMD8
  uint8_t crc = 0xFF;
  if (cmd == CMD0) {
    crc = 0x95; // Valid CRC for CMD0
  } else if (cmd == CMD8) {
    crc = 0x87; // Valid CRC for CMD8 (0x1AA)
  }

  // Wait for response
  uint8_t response = HAL->spi.transfer8(sdCardSpiDevice, crc);
  for (int ii = 0; ((response & 0x80) != 0) && (ii < 10); ii++) {
    response = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  }

  return response;
}

/// @fn uint16_t sdSpiCrc16Ccitt(const uint8_t *data, uint16_t length)
///
/// @brief CRC-16-CCITT (polynomial 0x1021, seed 0x0000) over a buffer - the
/// check word that every SD data block carries at its tail in SPI mode,
/// transmitted regardless of the CMD59 CRC_ON_OFF setting.
///
/// @param data The buffer to checksum.
/// @param length The number of bytes in the buffer.
///
/// @return The 16-bit CRC.
static uint16_t sdSpiCrc16Ccitt(const uint8_t *data, uint16_t length) {
  uint16_t crc = 0;
  for (uint16_t ii = 0; ii < length; ii++) {
    crc ^= (uint16_t) ((uint16_t) data[ii] << 8);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if ((crc & 0x8000) != 0) {
        crc = (uint16_t) ((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t) (crc << 1);
      }
    }
  }
  return crc;
}

/// @fn int sdSpiReadCsd(int sdCardSpiDevice, uint8_t csd[16], bool requireCrc)
///
/// @brief Issue CMD9 (SEND_CSD) and read the 16-byte CSD register, optionally
/// verifying the trailing CRC-16.
///
/// @details The SPI transfer is ended before this function returns in every
/// case - success or failure.
///
/// @param sdCardSpiDevice The zero-based SPI device ID.
/// @param csd A 16-byte buffer that receives the CSD.  Left untouched on an
///   R1 or data-token failure; populated (but suspect) on a CRC mismatch.
/// @param requireCrc When true, a CRC-16 mismatch fails the read; when false
///   the bytes are returned as-is and only the transport is checked.
///
/// @return 0 on success, negative errno on failure.
static int sdSpiReadCsd(int sdCardSpiDevice, uint8_t csd[16], bool requireCrc) {
  uint8_t response = sdSpiSendCommand(sdCardSpiDevice, CMD9, 0);
  if (response != 0x00) {
    HAL->spi.endTransfer(sdCardSpiDevice);
    return -EIO;
  }

  // Wait for the data token (0xFE).
  uint16_t timeoutCount = 10000;
  do {
    response = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
    if (response == 0xFE) {
      break;
    }
  } while (--timeoutCount != 0);
  if (timeoutCount == 0) {
    HAL->spi.endTransfer(sdCardSpiDevice);
    return -ETIMEDOUT;
  }

  for (int ii = 0; ii < 16; ii++) {
    csd[ii] = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  }

  uint8_t crcHigh = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  uint8_t crcLow  = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  HAL->spi.endTransfer(sdCardSpiDevice);

  if (requireCrc == true) {
    uint16_t received = (uint16_t) (((uint16_t) crcHigh << 8) | crcLow);
    if (sdSpiCrc16Ccitt(csd, 16) != received) {
      return -EIO;
    }
  }

  return 0;
}

/// @fn int sdSpiCardInit(SdCardSpiArgs *sdCardSpiArgs, SdCardState *sdCardState)
///
/// @brief Initialize the SD card for communication with the OS.
///
/// @details The whole card-identification handshake (CMD0 / CMD8 / ACMD41 /
/// CMD58 / CMD16) runs at SD_SPI_INIT_BAUD and the bus is left there on
/// return; picking the data-phase clock is sdSpiNegotiateFastBaud()'s job.
/// The >= 74-clock power-up sequence with the chip select deasserted is
/// emitted by HAL->spi.configure().
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the information needed to initialize the card.
/// @param sdCardState If non-NULL, sdCardState->blockAddressed is set from the
///   CMD58 OCR CCS bit.
///
/// @return Returns the version of the connected card on success (1 or 2),
/// 0 on error, or a negative errno.
int sdSpiCardInit(SdCardSpiArgs *sdCardSpiArgs, SdCardState *sdCardState) {
  uint8_t response;
  uint16_t timeoutCount;
  bool isSDv2 = false;
  bool blockAddressed = false;

  // Bring the device up at the identification-phase clock rate.  configure()
  // also drives the chip select high and emits the SD power-up clock sequence.
  int32_t initStatus = HAL->spi.configure(SD_CARD_SPI_DEVICE,
    sdCardSpiArgs->spiCsDio,
    sdCardSpiArgs->spiSckDio,
    sdCardSpiArgs->spiCopiDio,
    sdCardSpiArgs->spiCipoDio,
    SD_SPI_INIT_BAUD
  );
  if (initStatus != 0) {
    // Just pass the error upward.
    return initStatus;
  }

  // Send CMD0 to enter SPI mode
  timeoutCount = 200;  // Extended timeout
  do {
    for (int ii = 0; ii < 8; ii++) {  // More dummy clocks
      HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD0, 0);
    if (--timeoutCount == 0) {
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      logError("CMD0 timed out\n");
      return -ETIMEDOUT;
    }
  } while (response != R1_IDLE_STATE);
  
  // Send CMD8 to check version
  for (int ii = 0; ii < 8; ii++) {
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
  }
  response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD8, 0x000001AA);
  if (response == R1_IDLE_STATE) {
    isSDv2 = true;
    for (int ii = 0; ii < 4; ii++) {
      response = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
  }
  HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
  
  // Initialize card with ACMD41
  timeoutCount = 20000;  // Much longer timeout
  do {
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD55, 0);
    HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
    
    for (int ii = 0; ii < 8; ii++) {
      HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    
    // Try both with and without HCS bit based on card version
    uint32_t acmd41Arg = isSDv2 ? 0x40000000 : 0;
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, ACMD41, acmd41Arg);
    HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
    
    if (--timeoutCount == 0) {
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      logError("ACMD41 timed out\n");
      return -ETIMEDOUT;
    }
  } while (response != 0);

  // Start from the card-version heuristic (v1 -> byte addressed, v2 -> block
  // addressed).  CMD58 below only *refines* this, and only when the OCR it
  // hands back is unambiguously valid - a flaky READ_OCR must never be able to
  // flip a working card into the wrong addressing mode.
  blockAddressed = isSDv2;

  // Drain the trailing busy bytes from ACMD41 and, keeping CS asserted the same
  // way CMD0 flows into CMD8, send CMD58 (READ_OCR).  The dummy-clock loop here
  // also primes the bus for the command, exactly as the CMD0 / CMD8 / ACMD41
  // paths above do - without it the command byte races the CS falling edge and
  // the response can come back misaligned.
  for (int ii = 0; ii < 8; ii++) {
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
  }
  response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD58, 0);
  if (response == 0x00) {
    uint8_t ocr[4];
    for (int ii = 0; ii < 4; ii++) {
      ocr[ii] = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    // Only trust the CCS bit (OCR bit 30) when bit 31 (power-up complete) is
    // set - i.e. this really is an OCR and not an idle or half-shifted bus.
    if ((ocr[0] & 0x80) != 0) {
      blockAddressed = ((ocr[0] & 0x40) != 0); // CCS
    }
  } else {
    logError("CMD58 (READ_OCR) returned %ld; keeping %s addressing\n",
      (long int) response, isSDv2 ? "block" : "byte");
  }
  HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);

  // CMD16 (SET_BLOCKLEN = 512): only meaningful for byte-addressed cards -
  // block-addressed cards are fixed at 512 bytes and may reject it.  Primed
  // with dummy clocks like every other command here.
  if (blockAddressed == false) {
    for (int ii = 0; ii < 8; ii++) {
      HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    }
    response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, CMD16, 512);
    HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
    if (response != 0x00) {
      // Not fatal: most byte-addressed cards already default to 512.
      logError("CMD16 (SET_BLOCKLEN) returned %ld\n", (long int) response);
    }
  }

  // Handshake complete.  The bus stays at SD_SPI_INIT_BAUD; sdSpiNegotiateFastBaud()
  // ramps it up from here.
  if (sdCardState != NULL) {
    sdCardState->blockAddressed = blockAddressed;
  }
  return isSDv2 ? 2 : 1;
}

/// @fn uint32_t sdSpiNegotiateFastBaud(int sdCardSpiDevice)
///
/// @brief Find the fastest data-phase bus clock this hardware reads cleanly.
///
/// @details The identification phase has already completed at SD_SPI_INIT_BAUD,
/// so that rate is known good.  Starting from the current _sdSpiFastBaud (seeded
/// at SD_SPI_FAST_BAUD_MAX, so the first call after a cold boot begins at the
/// ceiling) this halves the requested clock and re-reads the CSD (CMD9) with a
/// CRC-16 check on each step.  The first rate whose CSD read passes wins.  If
/// nothing down to (but not below) SD_SPI_INIT_BAUD passes, the bus is left at
/// SD_SPI_INIT_BAUD.  A card-process restart keeps the last negotiated ceiling
/// rather than re-testing rates already known to fail on this board.
///
/// Note that each HAL clamps the request to its peripheral's real range, so on
/// some targets consecutive halvings map to the same actual clock; that just
/// costs one extra probe and is otherwise harmless.
///
/// @param sdCardSpiDevice The zero-based SPI device ID.
///
/// @return The negotiated baud.  Also stored in _sdSpiFastBaud and left applied
/// to the bus via HAL->spi.setSpeed().
static uint32_t sdSpiNegotiateFastBaud(int sdCardSpiDevice) {
  uint8_t csd[16];

  for (uint32_t baud = _sdSpiFastBaud;
    baud >= SD_SPI_INIT_BAUD;
    baud >>= 1
  ) {
    HAL->spi.setSpeed(sdCardSpiDevice, baud);
    int status = sdSpiReadCsd(sdCardSpiDevice, csd, true);
    if (status == 0) {
      _sdSpiFastBaud = baud;
      logDetail("SD: data phase negotiated to %ld baud\n", (long int) baud);
      return baud;
    }
    logDetail("SD: CMD9 probe failed at %ld baud (%s)\n",
      (long int) baud, strerror(-status));
  }

  _sdSpiFastBaud = SD_SPI_INIT_BAUD;
  HAL->spi.setSpeed(sdCardSpiDevice, SD_SPI_INIT_BAUD);
  logError("SD: no data-phase rate passed CRC; holding bus at %ld baud\n",
    (long int) SD_SPI_INIT_BAUD);
  return SD_SPI_INIT_BAUD;
}

/// @fn void sdSpiSendCmd12Inline(int sdCardSpiDevice)
///
/// @brief Send CMD12 (STOP_TRANSMISSION) on an already-active SPI transfer.
///
/// @details Unlike sdSpiSendCommand, this function does NOT call
/// startTransfer.  It is intended for use during a CMD18 or CMD25 multi-block
/// operation where the chip-select line is already asserted and must stay low
/// until the entire sequence is complete.
///
/// @param sdCardSpiDevice The zero-based SPI device ID to use.
static void sdSpiSendCmd12Inline(int sdCardSpiDevice) {
  // Command byte
  HAL->spi.transfer8(sdCardSpiDevice, CMD12 | 0x40);
  
  // Argument (0x00000000)
  HAL->spi.transfer8(sdCardSpiDevice, 0x00);
  HAL->spi.transfer8(sdCardSpiDevice, 0x00);
  HAL->spi.transfer8(sdCardSpiDevice, 0x00);
  HAL->spi.transfer8(sdCardSpiDevice, 0x00);
  
  // CRC (don't care)
  HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  
  // Discard the stuff byte that follows a CMD12 response.
  HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
  
  // Wait for the R1 response.
  for (int ii = 0; ii < 10; ii++) {
    uint8_t response = HAL->spi.transfer8(sdCardSpiDevice, 0xFF);
    if ((response & 0x80) == 0) {
      break;
    }
  }
  
  // Consume any remaining busy bytes (card holds MISO low).
  for (int ii = 0; ii < 10000; ii++) {
    if (HAL->spi.transfer8(sdCardSpiDevice, 0xFF) == 0xFF) {
      break;
    }
  }
}

/// @var _bulkreadCmd
///
/// @brief The command to use when reading more than one block.  Note that
/// this is "bulk read" as opposed to "multi-block read" because it isn't
/// necessarily multi-block.  It can be overridden to use CMD17 in
/// sdSpiReadBlocks if we detect that our underlying hardware doesn't support
/// CMD18.
static uint8_t _bulkreadCmd = CMD18;

/// @fn int sdSpiReadBlocks(SdCardState *sdCardState,
///   uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer)
///
/// @brief Read blocks from an SD card into a buffer.
///
/// @details For single-block reads this function uses CMD17
/// (READ_SINGLE_BLOCK).  For multi-block reads it issues CMD18
/// (READ_MULTIPLE_BLOCK) once and terminates the transfer with CMD12
/// (STOP_TRANSMISSION), avoiding per-block command overhead.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   runSdCard process.
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
  
  uint32_t address = startBlock;
  if (sdCardState->blockAddressed == false) {
    address *= sdCardState->blockSize; // Convert to byte address
  }
  
  // Choose the appropriate read command.
  uint8_t readCmd = (numBlocks == 1) ? CMD17 : _bulkreadCmd;
  uint8_t response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, readCmd, address);
  if (response != 0x00) {
    do {
      if (readCmd == CMD18) {
        // We attempted to start a multi-block read and it failed.  Try
        // single-block.
        readCmd = CMD17;
        response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, readCmd, address);
        if (response == 0x00) {
          // Single-block works, so we're good.  Don't return early.
          // Use CMD17 for the bulk read command so that we don't fall into this
          // case on every read from now on.
          _bulkreadCmd = CMD17;
          break;
        }
      }
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Command failed
    } while (0);
  }
  
  for (uint32_t ii = 0; ii < numBlocks; ii++) {
    // Wait for data token (0xFE)
    uint16_t timeout = 10000;
    while (timeout--) {
      response = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      if (response == 0xFE) {
        break;
      }
      if (timeout == 0) {
        // On a multi-block read we must still stop transmission.  Send CMD12
        // inline — we cannot use sdSpiSendCommand here because it would call
        // startTransfer again on an already-active SPI transfer.
        if (readCmd == CMD18) {
          sdSpiSendCmd12Inline(SD_CARD_SPI_DEVICE);
        }
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO;  // Timeout waiting for data
      }
    }
    
    // Read the block
    memset(buffer, 0xFF, sdCardState->blockSize);
    if (HAL->spi.transferBytes(
      SD_CARD_SPI_DEVICE, buffer, sdCardState->blockSize) != 0
    ) {
      if (readCmd == CMD18) {
        sdSpiSendCmd12Inline(SD_CARD_SPI_DEVICE);
      }
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Transfer failed
    }
    
    // Read CRC (2 bytes, ignored)
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
    
    buffer += sdCardState->blockSize;
    
    if ((ii < numBlocks - 1) && (readCmd == CMD17)) {
      address = startBlock + ii + 1;
      if (sdCardState->blockAddressed == false) {
        address *= sdCardState->blockSize; // Convert to byte address
      }
      response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, readCmd, address);
      if (response != 0x00) {
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO; // Command failed
      }
    }
  }
  
  // For multi-block reads, send CMD12 (STOP_TRANSMISSION) to end the stream.
  if (readCmd == CMD18) {
    sdSpiSendCmd12Inline(SD_CARD_SPI_DEVICE);
  }
  
  HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
  return 0;
}

/// @var _bulkWriteCmd
///
/// @brief The command to use when multiple blocks are going to be written.
/// Note that this is "bulk write" as opposed to "multi-block write" because
/// it isn't necessarily multi-block.  It can be overridden in sdSpiWriteBlocks
/// to be CMD24 if we detect that CMD25 isn't supported.
static uint8_t _bulkWriteCmd = CMD25;

/// @fn int sdSpiWriteBlocks(SdCardState *sdCardState,
///   uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer)
/// 
/// @brief Write a buffer of blocks to an SD card.
///
/// @details For single-block writes this function uses CMD24 (WRITE_BLOCK).
/// For multi-block writes it issues CMD25 (WRITE_MULTIPLE_BLOCK) once and
/// terminates the transfer with a Stop Tran token (0xFD), avoiding per-block
/// command overhead.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   runSdCard process.
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
  HAL->spi.startTransfer(SD_CARD_SPI_DEVICE);
  uint8_t response = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
  if (response != 0xFF) {
    HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
    return EIO;
  }
  
  uint32_t address = startBlock;
  if (sdCardState->blockAddressed == false) {
    address *= sdCardState->blockSize; // Convert to byte address
  }
  
  // Choose the appropriate write command.
  uint8_t writeCmd = (numBlocks == 1) ? CMD24 : _bulkWriteCmd;
  
  response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, writeCmd, address);
  if (response != 0x00) {
    do {
      if (writeCmd == CMD25) {
        // We attempted to start a multi-block write and it failed.  Try
        // single-block.
        writeCmd = CMD24;
        response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, writeCmd, address);
        if (response == 0x00) {
          // Single-block works, so we're good.  Don't return early.
          // Set _bulkWriteCmd to use CMD24 so that we don't fall into this case
          // on every write going forward.
          _bulkWriteCmd = CMD24;
          break;
        }
      }
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Command failed
    } while (0);
  }
  
  // CMD24 uses 0xFE as the start token; CMD25 uses 0xFC.
  uint8_t startToken = (writeCmd == CMD24) ? 0xFE : 0xFC;
  
  for (uint32_t ii = 0; ii < numBlocks; ii++) {
    // Wait for card to be ready before sending data
    uint16_t timeout = 10000;
    do {
      response = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      if (--timeout == 0) {
        if (writeCmd == CMD25) {
          // Send Stop Tran token to abort the multi-block write.
          HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFD);
          HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
        }
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO;
      }
    } while (response != 0xFF);
    
    // Send start token
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, startToken);
    
    // Write data
    if (HAL->spi.transferBytes(
      SD_CARD_SPI_DEVICE, buffer, sdCardState->blockSize) != 0
    ) {
      if (writeCmd == CMD25) {
        HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFD);
        HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      }
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Bad response
    }
    
    // We need the response token which, per spec, could actually come in on
    // the last byte transferred for the block.  Grab that as the first
    // candidate.
    response = buffer[sdCardState->blockSize - 1];
    
    // Always transfer at least two bytes for the CRC after the block.  The
    // response token could actually be one of them since we're not using CRC
    // in this implementation.  If the response token isn't one of them, poll
    // for up to 10 more cycles after that.
    for (int jj = 0;
      (jj < 2) || (((response & 0x1F) != 0x05) && (jj < 12));
      jj++
    ) {
      if ((response & 0x1F) == 0x05) {
        HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      } else {
        response = HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      }
    }
    if ((response & 0x1F) != 0x05) {
      if (writeCmd == CMD25) {
        HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFD);
        HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
      }
      HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
      return EIO; // Bad response
    }
    
    // Wait for write to complete (card holds MISO low while busy)
    timeout = 10000;
    while (timeout--) {
      if (HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF) != 0x00) {
        break;
      }
      if (timeout == 0) {
        if (writeCmd == CMD25) {
          HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFD);
          HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF);
        }
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO; // Write timeout
      }
    }
    
    buffer += sdCardState->blockSize;
    
    if ((ii < numBlocks - 1) && (writeCmd == CMD24)) {
      address = startBlock + ii + 1;
      if (sdCardState->blockAddressed == false) {
        address *= sdCardState->blockSize; // Convert to byte address
      }
      response = sdSpiSendCommand(SD_CARD_SPI_DEVICE, writeCmd, address);
      if (response != 0x00) {
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO; // Command failed
      }
    }
  }
  
  // For multi-block writes, send the Stop Tran token (0xFD) and wait for the
  // card to finish programming.
  if (writeCmd == CMD25) {
    HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFD);
    // Wait for card to leave busy state.
    uint16_t timeout = 10000;
    while (timeout--) {
      if (HAL->spi.transfer8(SD_CARD_SPI_DEVICE, 0xFF) != 0x00) {
        break;
      }
      if (timeout == 0) {
        HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
        return EIO;
      }
    }
  }
  
  HAL->spi.endTransfer(SD_CARD_SPI_DEVICE);
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
  uint8_t csd[16];
  if (sdSpiReadCsd(sdCardSpiDevice, csd, false) != 0) {
    logError("CMD9 (SEND_CSD) failed; assuming 512 bytes per block\n");
    return 512;
  }

  // For CSD Version 1.0 and 2.0, READ_BL_LEN is at the same location.
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

  if (sdSpiReadCsd(sdCardSpiDevice, cardSpecificData, false) != 0) {
    logError("CMD9 (SEND_CSD) failed; assuming %ld blocks\n",
      ((long int) 1024) * ((long int) 1024));
    return ((long int) 1024) * ((long int) 1024);
  }

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

/// @fn int sdCardSpiReadBlocksCommandHandler(
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
int sdCardSpiReadBlocksCommandHandler(
  SdCardState *sdCardState, ProcessMessage *processMessage
) {
  SdCommandArgs *sdCommandArgs
    = (SdCommandArgs*) processMessageData(processMessage);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteArgs(
    sdCardState, sdCommandArgs, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandArgs->buffer;
    returnValue = sdSpiReadBlocks(sdCardState,
      startSdBlock, numSdBlocks, buffer);
  }

  processMessageData(processMessage) = (void*) ((intptr_t) returnValue);
  processMessageSetDone(processMessage);

  return 0;
}

/// @fn int sdCardSpiWriteBlocksCommandHandler(
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
int sdCardSpiWriteBlocksCommandHandler(
  SdCardState *sdCardState, ProcessMessage *processMessage
) {
  SdCommandArgs *sdCommandArgs
    = (SdCommandArgs*) processMessageData(processMessage);
  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteArgs(
    sdCardState, sdCommandArgs, &startSdBlock, &numSdBlocks);

  if (returnValue == 0) {
    uint8_t *buffer = sdCommandArgs->buffer;
    returnValue = sdSpiWriteBlocks(sdCardState,
      startSdBlock, numSdBlocks, buffer);
  }

  processMessageData(processMessage) = (void*) ((intptr_t) returnValue);
  processMessageSetDone(processMessage);

  return 0;
}

/// @var sdCardSpiCommandHandlers
///
/// @brief Array of SdCardCommandHandler function pointers to handle commands
/// received by the runSdCard function.
KEEP_IN_FLASH
const SdCardCommandHandler sdCardSpiCommandHandlers[] = {
  sdCardSpiReadBlocksCommandHandler,         // SD_CARD_READ_BLOCKS
  sdCardSpiWriteBlocksCommandHandler,        // SD_CARD_WRITE_BLOCKS
};

/// @fn void handleSdCardSpiMessages(SdCardState *sdCardState)
///
/// @brief Handle sdCard messages from the process's queue until there are no
/// more waiting.
///
/// @param sdCardState A pointer to the SdCardState structure maintained by the
///   sdCard process.
///
/// @return This function returns no value.
void handleSdCardSpiMessages(SdCardState *sdCardState) {
  ProcessMessage *processMessage = processMessageQueueWait(NULL);
  while (processMessage != NULL) {
    if ((processMessageType(processMessage) & 0xffffffffffffff00)
      != SD_CARD_COMMAND_SIGNATURE
    ) {
      logError("Received unknown signature 0x%lx from process %d\n",
        (unsigned long int)
          (processMessageType(processMessage) & 0xffffffffffffff00),
        processPid(processMessageFrom(processMessage)));
      // Don't attempt to process this message further.
      processMessage = processMessageQueuePop();
      continue;
    }

    SdCardCommandResponse messageType
      = (SdCardCommandResponse) (processMessageType(processMessage) & 0xff);
    if (messageType >= NUM_SD_CARD_COMMANDS) {
      logError("Invalid message type %ld\n", (long int) messageType);

      processMessage = processMessageQueuePop();
      continue;
    }
    
    sdCardSpiCommandHandlers[messageType](sdCardState, processMessage);
    processMessage = processMessageQueuePop();
  }
  
  return;
}

/// @fn void* runSdCardSpi(void *args)
///
/// @brief Process entry-point for the SD card process.  Sets up and
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
  BlockDevice blockStorageDevice = {
    .context = (void*) ((intptr_t) getRunningPid()),
    .readBlocks = sdReadBlocks,
    .writeBlocks = sdWriteBlocks,
    .schedReadBlocks = schedSdReadBlocks,
    .schedWriteBlocks = schedSdWriteBlocks,
    .blockSize = 0,
    .blockBitShift = 0,
    .partitionNumber = 0,
  };
  sdCardState.bsDevice = &blockStorageDevice;

  sdCardState.sdCardVersion = sdSpiCardInit(sdCardSpiArgs, &sdCardState);
  if (sdCardState.sdCardVersion > 0) {
    // Ramp the bus from the (known-good) identification clock up to the fastest
    // rate this board / card / wiring reads without corrupting data.
    sdSpiNegotiateFastBaud(SD_CARD_SPI_DEVICE);
    sdCardState.blockSize = blockStorageDevice.blockSize
      = sdSpiGetBlockSize(SD_CARD_SPI_DEVICE);
    sdCardState.numBlocks = sdSpiGetBlockCount(SD_CARD_SPI_DEVICE);
#ifdef SD_CARD_DEBUG
    logDetail("Card is %s\n",
      sdCardState.blockAddressed ? "SDHC/SDXC (block addressed)"
                                 : "SDSC (byte addressed)");
    logDetail("Data-phase bus clock = %ld baud\n", (long int) _sdSpiFastBaud);
    logDetail("Card block size = %ld\n",
      (long int) blockStorageDevice.blockSize);
    logDetail("%ld total blocks (%ld total bytes)\n",
      (long int) sdCardState.numBlocks, ((long int) sdCardState.numBlocks)
        * ((long int) sdCardState.blockSize));
#endif // SD_CARD_DEBUG
  } else {
    logError("sdSpiCardInit returned status: %s\n",
      strerror(-sdCardState.sdCardVersion));
  }
  processYieldValue(&blockStorageDevice);

  while (1) {
    processYield();
    handleSdCardSpiMessages(&sdCardState);
  }

  return NULL;
}

