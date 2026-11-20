///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_graphics_header.c
///
/// @brief             Tests of Graphics.h, the client side of the display
///                    driver protocol, against a fake driver:  the transport,
///                    argument storage and sharing, command records, submits
///                    and preuploads, and the client's font cache and text
///                    measurement.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"
#include "HalMock.h"

#include <string.h>

#include "kernel/Graphics.h"
#include "kernel/MessageArgs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsSignal.h"

int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType);

/// @def MAX_RECEIVED
///
/// @brief How many messages the fake driver records.
#define MAX_RECEIVED 24

/// @def MAX_ARGS_BYTES
///
/// @brief How many bytes of each message's arguments the fake driver records.
#define MAX_ARGS_BYTES 32

/// @def MAX_COMMAND_BYTES
///
/// @brief How many bytes of each submit's or preupload's commands the fake
/// driver records.
#define MAX_COMMAND_BYTES 512

/// @def MAX_COMMAND_LISTS
///
/// @brief How many submits and preuploads the fake driver records the
/// commands of.
#define MAX_COMMAND_LISTS 4

/// @def FIRST_HANDLE
///
/// @brief The first handle the fake driver gives out.
#define FIRST_HANDLE 0x100

/// @enum DriverMode
///
/// @brief What the fake driver does with each message:  answer it, exit or
/// kill itself after popping it, keep it forever, answer it after
/// holdYields yields, hold each submit until the next one arrives, or write
/// into a font's bitmap before answering.
typedef enum DriverMode {
  DRIVER_ANSWER,
  DRIVER_EXIT,
  DRIVER_KILL_SELF,
  DRIVER_HOLD_FOREVER,
  DRIVER_HOLD_YIELDS,
  DRIVER_HOLD_SUBMITS,
  DRIVER_WRITE_IN,
} DriverMode;

/// @struct Received
///
/// @brief What the fake driver saw of one message, when it popped it.
typedef struct Received {
  int64_t type;
  size_t  size;
  bool    waiting;
  uint8_t args[MAX_ARGS_BYTES];
} Received;

static Received received[MAX_RECEIVED];
static volatile int numReceived;
static uint8_t commands[MAX_COMMAND_LISTS][MAX_COMMAND_BYTES];
static uint32_t commandSizes[MAX_COMMAND_LISTS];
static volatile int numCommandLists;
static volatile int driverMode;
static volatile int holdYields;
static volatile uint8_t answerStatus;
static volatile int clientStateWhileHeld;
static volatile bool heldReleased;
static volatile int nextHandle;
static volatile int killResult;
static ProcessId clientPid;
static char longText[70001];

/// @fn static void finish(ProcessMessage *processMessage)
///
/// @brief Mark a message done, and release it if nobody waits for it.
///
/// @param processMessage The message.
///
/// @return This function returns no value.
static void finish(ProcessMessage *processMessage) {
  processMessageSetDone(processMessage);
  if (processMessageWaiting(processMessage) == false) {
    processMessageRelease(processMessage);
  }
}

/// @fn static void recordMessage(ProcessMessage *processMessage)
///
/// @brief Record a message's type, size, waiting flag and argument bytes, and
/// the command bytes of a submit or preupload.
///
/// @param processMessage The message.
///
/// @return This function returns no value.
static void recordMessage(ProcessMessage *processMessage) {
  void *args = processMessageData(processMessage);
  uint8_t type = (uint8_t) processMessageType(processMessage);
  if (numReceived < MAX_RECEIVED) {
    Received *receivedMessage = &received[numReceived];
    receivedMessage->type = processMessageType(processMessage);
    receivedMessage->size = processMessageSize(processMessage);
    receivedMessage->waiting = processMessageWaiting(processMessage);
    memset(receivedMessage->args, 0, sizeof(receivedMessage->args));
    if (args != NULL) {
      size_t size = receivedMessage->size;
      memcpy(receivedMessage->args, args,
        (size < sizeof(receivedMessage->args))
          ? size : sizeof(receivedMessage->args));
    }
  }
  numReceived++;

  const uint8_t *data = NULL;
  uint32_t size = 0;
  if (type == GFX_MSG_SUBMIT) {
    data = ((GfxSubmitArgs*) args)->cmdData;
    size = ((GfxSubmitArgs*) args)->cmdSize;
  } else if (type == GFX_MSG_PREUPLOAD) {
    data = ((GfxPreuploadArgs*) args)->cmdData;
    size = ((GfxPreuploadArgs*) args)->cmdSize;
  }
  if ((data != NULL) && (numCommandLists < MAX_COMMAND_LISTS)) {
    commandSizes[numCommandLists] = size;
    memcpy(commands[numCommandLists], data,
      (size < MAX_COMMAND_BYTES) ? size : MAX_COMMAND_BYTES);
  }
  if (data != NULL) {
    numCommandLists++;
  }
}

/// @fn static void answer(uint8_t type, void *args, bool waiting)
///
/// @brief Answer a message as a driver that implements everything would:
/// a synchronous one gets answerStatus and fixed outputs, a one-way one has
/// its arguments destroyed.
///
/// @param type The GfxMsgType.
/// @param args The message's arguments.
/// @param waiting Whether the sender waits for it.
///
/// @return This function returns no value.
static void answer(uint8_t type, void *args, bool waiting) {
  if (waiting == false) {
    if (args != NULL) {
      MESSAGE_ARGS_DESTROY(args);
    }
    return;
  }

  *((uint8_t*) args) = answerStatus;
  if (type == GFX_MSG_OPEN_DISPLAY) {
    GfxOpenDisplayArgs *open = (GfxOpenDisplayArgs*) args;
    open->width = 640;
    open->height = 480;
    open->defaultFontW = 6;
    open->defaultFontH = 8;
    open->defaultFontFirst = 0;
    open->defaultFontCount = 256;
  } else if (type == GFX_MSG_GET_SURFACE_SIZE) {
    ((GfxSurfaceSizeArgs*) args)->width = 320;
    ((GfxSurfaceSizeArgs*) args)->height = 200;
  } else if (type == GFX_MSG_GET_IMAGE_SIZE) {
    ((GfxImageSizeArgs*) args)->width = 12;
    ((GfxImageSizeArgs*) args)->height = 34;
  } else if (type == GFX_MSG_UPLOAD_IMAGE) {
    ((GfxUploadImageArgs*) args)->handle = (GfxHandle) nextHandle++;
  } else if (type == GFX_MSG_UPLOAD_FONT) {
    ((GfxUploadFontArgs*) args)->handle = (GfxHandle) nextHandle++;
  } else if (type == GFX_MSG_PREUPLOAD) {
    ((GfxPreuploadArgs*) args)->handle = (GfxHandle) nextHandle++;
  } else if (type == GFX_MSG_CAPTURE_RECT) {
    ((GfxCaptureRectArgs*) args)->handle = (GfxHandle) nextHandle++;
  } else if (type == GFX_MSG_GET_ERROR) {
    ((GfxGetErrorArgs*) args)->error = GFX_ERR_INVALID_HANDLE;
  }
}

/// @fn static void* fakeDriver(void *args)
///
/// @brief Stand in for the display driver, recording every graphics message
/// and handling it as driverMode says.
///
/// @param args Unused.
///
/// @return Returns NULL in DRIVER_EXIT mode and never returns otherwise.
static void* fakeDriver(void *args) {
  (void) args;
  ProcessMessage *held = NULL;
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    if (processMessage == NULL) {
      continue;
    }
    if ((processMessageType(processMessage) & ~((int64_t) 0xff))
      != GFX_COMMAND_SIGNATURE
    ) {
      finish(processMessage);
      continue;
    }
    uint8_t type = (uint8_t) processMessageType(processMessage);
    void *messageArgs = processMessageData(processMessage);
    bool waiting = processMessageWaiting(processMessage);
    if ((held != NULL) && (driverMode != DRIVER_HOLD_SUBMITS)) {
      answer(GFX_MSG_SUBMIT, processMessageData(held), true);
      finish(held);
      held = NULL;
    }
    recordMessage(processMessage);

    if (driverMode == DRIVER_EXIT) {
      return NULL;
    } else if (driverMode == DRIVER_KILL_SELF) {
      SchedulerKillProcessArgs kill = {
        .pid = getRunningPid(), .signal = SIGKILL,
        .returnValue = 0, .errorNumber = 0,
      };
      ProcessMessage *killMessage = initSendProcessMessageToPid(schedulerPid,
        SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
        &kill, sizeof(kill), true);
      if (killMessage != NULL) {
        processMessageWaitForDone(killMessage, NULL);
      }
      killResult = (killMessage != NULL) ? kill.returnValue : -1000;
      return NULL;
    } else if (driverMode == DRIVER_HOLD_FOREVER) {
      while (1) {
        processYield();
      }
    } else if (driverMode == DRIVER_HOLD_YIELDS) {
      for (int ii = 0; ii < holdYields; ii++) {
        processYield();
      }
      clientStateWhileHeld = processState(&allProcesses[clientPid - 1]);
    } else if ((driverMode == DRIVER_HOLD_SUBMITS)
      && (type == GFX_MSG_SUBMIT)
    ) {
      if (held == NULL) {
        held = processMessage;
        continue;
      }
      for (int ii = 0; ii < 20; ii++) {
        processYield();
      }
      clientStateWhileHeld = processState(&allProcesses[clientPid - 1]);
      heldReleased = true;
      answer(type, processMessageData(held), true);
      finish(held);
      held = processMessage;
      continue;
    } else if ((driverMode == DRIVER_WRITE_IN)
      && (type == GFX_MSG_UPLOAD_FONT)
    ) {
      ((uint8_t*) ((GfxUploadFontArgs*) messageArgs)->bitmap)[0] ^= 0xff;
    }

    answer(type, messageArgs, waiting);
    finish(processMessage);
  }
  return NULL;
}

/// @fn static void graphicsPreBoot(void)
///
/// @brief Register the first user slot as the display driver.
///
/// @return This function returns no value.
static void graphicsPreBoot(void) {
  halMockSetGraphics(0, "/system/commands/gfxclient");
}

/// @fn static ProcessId driverSlotPid(void)
///
/// @brief Get the ProcessId graphicsPreBoot registered as the driver.
///
/// @return Returns the driver's ProcessId.
static ProcessId driverSlotPid(void) {
  return schedulerGetFirstShellPid() + schedulerGetNumShells();
}

/// @fn static void resetDriver(DriverMode mode)
///
/// @brief Forget everything the fake driver has recorded and set its mode.
///
/// @param mode The DriverMode.
///
/// @return This function returns no value.
static void resetDriver(DriverMode mode) {
  numReceived = 0;
  numCommandLists = 0;
  driverMode = mode;
  answerStatus = GFX_OK;
  clientStateWhileHeld = -1;
  heldReleased = false;
}

/// @fn static ProcessDescriptor* startDriver(DriverMode mode)
///
/// @brief Start fakeDriver in the registered slot, and let the test process
/// send it every client message type.
///
/// @param mode The DriverMode to start in.
///
/// @return Returns a pointer to the driver's ProcessDescriptor, or NULL on
/// failure.
static ProcessDescriptor* startDriver(DriverMode mode) {
  resetDriver(mode);
  nextHandle = FIRST_HANDLE;
  clientPid = getRunningPid();
  ProcessDescriptor *processDescriptor = &allProcesses[driverSlotPid() - 1];
  processQueueRemove(halMockFreeQueue(), processDescriptor);
  if (processCreate(processDescriptor, fakeDriver, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->userId = SYSTEM_USER_ID;
  processDescriptor->readyQueue
    = halMockReadyQueue(PRIVILEGE_LEVEL_EXECUTIVE);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  for (uint32_t type = 0; type < GFX_MSG_COUNT; type++) {
    if (addProcessIpcCapability(getRunningProcess(),
      processDescriptor->processId, GFX_COMMAND_SIGNATURE, type) != 0
    ) {
      return NULL;
    }
  }
  return processDescriptor;
}

/// @fn static bool openDisplay(GfxDisplay *disp)
///
/// @brief Open the display through the fake driver, then forget the open.
///
/// @param disp The display to open.
///
/// @return Returns true if the open succeeded.
static bool openDisplay(GfxDisplay *disp) {
  GfxError status = gfxOpenDisplay(disp, GFX_DRIVER_PROCESS_NAME, 640, 480);
  resetDriver((DriverMode) driverMode);
  return status == GFX_OK;
}

/// @fn static int64_t gfxType(uint8_t type)
///
/// @brief Get the full message type of a GfxMsgType.
///
/// @param type The GfxMsgType.
///
/// @return Returns the type under GFX_COMMAND_SIGNATURE.
static int64_t gfxType(uint8_t type) {
  return GFX_COMMAND_SIGNATURE | type;
}

/// @fn static bool bytesEqual(const uint8_t *expected, const uint8_t *actual,
///   size_t size)
///
/// @brief Compare two byte strings, recording the first difference as a
/// failure.
///
/// @param expected The expected bytes.
/// @param actual The actual bytes.
/// @param size The number of bytes.
///
/// @return Returns true if they're equal.
static bool bytesEqual(const uint8_t *expected, const uint8_t *actual,
  size_t size
) {
  for (size_t ii = 0; ii < size; ii++) {
    if (expected[ii] != actual[ii]) {
      nanoOsTestRecordFailure(__FILE__, __LINE__,
        "byte %zu: expected 0x%02x, got 0x%02x", ii, expected[ii], actual[ii]);
      return false;
    }
  }
  return true;
}

// --- transport -----------------------------------------------------------

NANO_OS_KERNEL_TEST_WITH(graphics_header, every_message_carries_the_signature_kind_and_size,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_ANSWER);
  GfxHandle handle = GFX_INVALID_HANDLE;
  GfxRect rect = { 1, 2, 3, 4 };
  uint16_t width = 0, height = 0;
  uint8_t pixels[4] = { 0 };
  uint8_t glyphs[8] = { 0 };
  GfxImageDesc image = { 2, 2, GFX_FMT_RGBA2222, pixels, sizeof(pixels) };
  GfxFontDesc font = { 8, 8, 'A', 1, glyphs, sizeof(glyphs) };

  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxGetSurfaceSize(&disp, &width, &height));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadImage(&disp, &image, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxDestroyImage(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK,
    gfxGetImageSize(&disp, handle, &width, &height));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &font, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetDefaultFont(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetDefaultFont(&disp, GFX_INVALID_HANDLE));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxDestroyFont(&disp, handle));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 64);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, 0);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxPreupload(buf, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxDiscardPreupload(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxFinish(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxCaptureRect(&disp, &rect, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_INVALID_HANDLE, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetCursor(&disp, handle, 1, 2));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxShowCursor(&disp, false));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxWarpCursor(&disp, 5, 6));
  NANO_OS_ASSERT_NULL(gfxCmdBufDestroy(buf));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxCloseDisplay(&disp));
  for (int ii = 0; (ii < 100) && (numReceived < 20); ii++) {
    processYield();
  }

  const struct { uint8_t type; bool waiting; size_t size; } expected[] = {
    { GFX_MSG_GET_SURFACE_SIZE,  true,  sizeof(GfxSurfaceSizeArgs) },
    { GFX_MSG_UPLOAD_IMAGE,      true,  sizeof(GfxUploadImageArgs) },
    { GFX_MSG_DESTROY_IMAGE,     false, sizeof(GfxHandleArgs) },
    { GFX_MSG_GET_IMAGE_SIZE,    true,  sizeof(GfxImageSizeArgs) },
    { GFX_MSG_UPLOAD_FONT,       true,  sizeof(GfxUploadFontArgs) },
    { GFX_MSG_SET_DEFAULT_FONT,  true,  sizeof(GfxSetDefaultFontArgs) },
    { GFX_MSG_SET_DEFAULT_FONT,  true,  sizeof(GfxSetDefaultFontArgs) },
    { GFX_MSG_DESTROY_FONT,      false, sizeof(GfxHandleArgs) },
    { GFX_MSG_SUBMIT,            true,  sizeof(GfxSubmitArgs) },
    { GFX_MSG_PREUPLOAD,         true,  sizeof(GfxPreuploadArgs) },
    { GFX_MSG_DISCARD_PREUPLOAD, false, sizeof(GfxHandleArgs) },
    { GFX_MSG_FINISH,            true,  sizeof(GfxFinishArgs) },
    { GFX_MSG_CAPTURE_RECT,      true,  sizeof(GfxCaptureRectArgs) },
    { GFX_MSG_GET_ERROR,         true,  sizeof(GfxGetErrorArgs) },
    { GFX_MSG_SET_CURSOR,        true,  sizeof(GfxSetCursorArgs) },
    { GFX_MSG_SHOW_CURSOR,       false, sizeof(GfxShowCursorArgs) },
    { GFX_MSG_WARP_CURSOR,       false, sizeof(GfxWarpCursorArgs) },
    { GFX_MSG_CLOSE_DISPLAY,     false, 0 },
  };
  size_t count = sizeof(expected) / sizeof(expected[0]);
  NANO_OS_ASSERT_EQ_INT((int) count, numReceived);
  for (size_t ii = 0; ii < count; ii++) {
    NANO_OS_ASSERT_TRUE(received[ii].type == gfxType(expected[ii].type));
    NANO_OS_ASSERT_EQ_INT(expected[ii].waiting, received[ii].waiting);
    NANO_OS_ASSERT_EQ_INT((int) expected[ii].size, (int) received[ii].size);
  }
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, opening_a_driver_name_nobody_has_fails_cleanly,
  graphicsPreBoot
) {
  GfxDisplay disp;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST,
    gfxOpenDisplay(&disp, "nonexistent", 640, 480));
  NANO_OS_ASSERT_EQ_INT(PROCESS_ID_NOT_SET, disp.driverPid);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, opening_finds_the_driver_and_caches_what_it_answers,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver(DRIVER_ANSWER);
  NANO_OS_ASSERT_NOT_NULL(driver);
  GfxDisplay disp;
  NANO_OS_ASSERT_EQ_INT(GFX_OK,
    gfxOpenDisplay(&disp, GFX_DRIVER_PROCESS_NAME, 800, 600));
  NANO_OS_ASSERT_EQ_INT(driver->processId, disp.driverPid);
  NANO_OS_ASSERT_EQ_INT(640, disp.width);
  NANO_OS_ASSERT_EQ_INT(480, disp.height);
  uint8_t width = 0, height = 0;
  gfxGetGlyphSize(&disp, GFX_INVALID_HANDLE, &width, &height);
  NANO_OS_ASSERT_EQ_INT(6, width);
  NANO_OS_ASSERT_EQ_INT(8, height);
  GfxOpenDisplayArgs sent;
  memcpy(&sent, received[0].args, sizeof(sent));
  NANO_OS_ASSERT_EQ_INT(800, sent.width);
  NANO_OS_ASSERT_EQ_INT(600, sent.height);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_driver_that_exits_before_answering_is_device_lost,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_EXIT);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(1, numReceived);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(1, numReceived);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_driver_killed_before_answering_is_device_lost,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver(DRIVER_ANSWER);
  NANO_OS_ASSERT_NOT_NULL(driver);
  NANO_OS_ASSERT_EQ_INT(0, addProcessIpcCapability(driver, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_KILL_PROCESS));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_KILL_SELF);
  killResult = 1;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(1, killResult);
  NANO_OS_ASSERT_EQ_INT(1, numReceived);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_slow_driver_still_answers,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_HOLD_YIELDS);
  holdYields = 2000;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_INVALID_HANDLE, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(PROCESS_STATE_WAIT, clientStateWhileHeld);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_driver_that_isnt_running_is_device_lost_without_a_leak,
  graphicsPreBoot
) {
  GfxDisplay disp;
  memset(&disp, 0, sizeof(disp));
  disp.driverPid = driverSlotPid();
  uint16_t live = messageArgsCounts()->live;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxDestroyImage(&disp, 5));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxShowCursor(&disp, true));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, gfxCloseDisplay(&disp));
  NANO_OS_ASSERT_EQ_INT(live, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

// --- argument structures and storage --------------------------------------

NANO_OS_KERNEL_TEST_WITH(graphics_header, the_driver_sees_inputs_and_a_preset_status,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, received[0].args[0]);

  GfxHandle handle = GFX_INVALID_HANDLE;
  uint16_t width = 0, height = 0;
  uint8_t pixels[6] = { 0 };
  uint8_t glyphs[16] = { 0 };
  GfxImageDesc image = { 3, 2, GFX_FMT_RGBA2222, pixels, sizeof(pixels) };
  GfxFontDesc font = { 6, 8, 'a', 2, glyphs, sizeof(glyphs) };
  GfxRect rect = { 10, 20, 30, 40 };
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxGetSurfaceSize(&disp, &width, &height));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadImage(&disp, &image, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK,
    gfxGetImageSize(&disp, 0x1234, &width, &height));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &font, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetDefaultFont(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxFinish(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxCaptureRect(&disp, &rect, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_INVALID_HANDLE, gfxGetError(&disp));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetCursor(&disp, 0x0405, 7, 9));
  NANO_OS_ASSERT_EQ_INT(9, numReceived);
  for (int ii = 0; ii < 9; ii++) {
    NANO_OS_ASSERT_EQ_INT(GFX_ERR_DEVICE_LOST, received[ii].args[0]);
  }

  GfxUploadImageArgs uploadImage;
  memcpy(&uploadImage, received[1].args, sizeof(uploadImage));
  NANO_OS_ASSERT_EQ_INT(GFX_FMT_RGBA2222, uploadImage.format);
  NANO_OS_ASSERT_EQ_INT(3, uploadImage.width);
  NANO_OS_ASSERT_EQ_INT(2, uploadImage.height);
  NANO_OS_ASSERT_EQ_INT(6, uploadImage.size);
  NANO_OS_ASSERT_NOT_NULL(uploadImage.pixels);
  GfxImageSizeArgs imageSize;
  memcpy(&imageSize, received[2].args, sizeof(imageSize));
  NANO_OS_ASSERT_EQ_INT(0x1234, imageSize.handle);
  GfxUploadFontArgs uploadFont;
  memcpy(&uploadFont, received[3].args, sizeof(uploadFont));
  NANO_OS_ASSERT_EQ_INT(6, uploadFont.glyphW);
  NANO_OS_ASSERT_EQ_INT(8, uploadFont.glyphH);
  NANO_OS_ASSERT_EQ_INT('a', uploadFont.firstChar);
  NANO_OS_ASSERT_EQ_INT(2, uploadFont.numChars);
  NANO_OS_ASSERT_EQ_INT(16, uploadFont.size);
  NANO_OS_ASSERT_NOT_NULL(uploadFont.bitmap);
  GfxSetDefaultFontArgs setDefault;
  memcpy(&setDefault, received[4].args, sizeof(setDefault));
  NANO_OS_ASSERT_EQ_INT(FIRST_HANDLE + 1, setDefault.handle);
  GfxCaptureRectArgs capture;
  memcpy(&capture, received[6].args, sizeof(capture));
  NANO_OS_ASSERT_EQ_INT(10, capture.rect.x);
  NANO_OS_ASSERT_EQ_INT(20, capture.rect.y);
  NANO_OS_ASSERT_EQ_INT(30, capture.rect.w);
  NANO_OS_ASSERT_EQ_INT(40, capture.rect.h);
  GfxSetCursorArgs cursor;
  memcpy(&cursor, received[8].args, sizeof(cursor));
  NANO_OS_ASSERT_EQ_INT(0x0405, cursor.image);
  NANO_OS_ASSERT_EQ_INT(7, cursor.hotX);
  NANO_OS_ASSERT_EQ_INT(9, cursor.hotY);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, the_client_returns_what_the_driver_wrote,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  nextHandle = 0x2345;
  GfxRect rect = { 0, 0, 8, 8 };
  GfxHandle handle = GFX_INVALID_HANDLE;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxCaptureRect(&disp, &rect, &handle));
  NANO_OS_ASSERT_EQ_INT(0x2345, handle);
  uint16_t width = 0, height = 0;
  NANO_OS_ASSERT_EQ_INT(GFX_OK,
    gfxGetImageSize(&disp, handle, &width, &height));
  NANO_OS_ASSERT_EQ_INT(12, width);
  NANO_OS_ASSERT_EQ_INT(34, height);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxGetSurfaceSize(&disp, &width, &height));
  NANO_OS_ASSERT_EQ_INT(320, width);
  NANO_OS_ASSERT_EQ_INT(200, height);
  NANO_OS_ASSERT_EQ_INT(320, disp.width);
  NANO_OS_ASSERT_EQ_INT(200, disp.height);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_failed_call_writes_no_outputs,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  answerStatus = GFX_ERR_NOT_SUPPORTED;
  GfxRect rect = { 0, 0, 8, 8 };
  GfxHandle handle = 0x7777;
  uint16_t width = 99, height = 98;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_NOT_SUPPORTED,
    gfxCaptureRect(&disp, &rect, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_INVALID_HANDLE, handle);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_NOT_SUPPORTED,
    gfxGetSurfaceSize(&disp, &width, &height));
  NANO_OS_ASSERT_EQ_INT(99, width);
  NANO_OS_ASSERT_EQ_INT(98, height);
  NANO_OS_ASSERT_EQ_INT(640, disp.width);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_driver_writing_an_in_buffer_is_reported_once,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_WRITE_IN);
  messageArgsResetCounts();
  uint8_t glyphs[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  GfxFontDesc font = { 8, 8, 'A', 1, glyphs, sizeof(glyphs) };
  GfxHandle handle = GFX_INVALID_HANDLE;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &font, &handle));
  NANO_OS_ASSERT_EQ_INT(1,
    messageArgsCounts()->errors[MESSAGE_ARGS_ERROR_IN_MODIFIED]);
  NANO_OS_ASSERT_EQ_INT(1, messageArgsErrorTotal());
  NANO_OS_ASSERT_EQ_INT(1, glyphs[0]);
  messageArgsResetCounts();
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, one_way_arguments_are_left_to_the_driver,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  messageArgsResetCounts();
  uint16_t live = messageArgsCounts()->live;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxDestroyImage(&disp, 5));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxWarpCursor(&disp, 3, 4));
  for (int ii = 0; (ii < 100) && (numReceived < 2); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_EQ_INT(2, numReceived);
  GfxHandleArgs destroy;
  memcpy(&destroy, received[0].args, sizeof(destroy));
  NANO_OS_ASSERT_EQ_INT(5, destroy.handle);
  GfxWarpCursorArgs warp;
  memcpy(&warp, received[1].args, sizeof(warp));
  NANO_OS_ASSERT_EQ_INT(3, warp.x);
  NANO_OS_ASSERT_EQ_INT(4, warp.y);
  NANO_OS_ASSERT_EQ_INT(live, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

// --- command records ------------------------------------------------------

NANO_OS_KERNEL_TEST_WITH(graphics_header, every_record_has_its_exact_encoding,
  graphicsPreBoot
) {
  GfxDisplay disp;
  memset(&disp, 0, sizeof(disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 256);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  GfxRect rect = { 1, 2, 3, 4 };
  GfxRect scissor = { 10, 20, 30, 40 };
  GfxPoint first = { 1, 2 }, second = { 3, 4 }, third = { 5, 6 };
  GfxPoint origin = { -1, 5 };
  GfxPoint center = { 7, 8 };
  gfxCmdSetViewport(buf, &rect);
  gfxCmdSetOrigin(buf, origin);
  gfxCmdPushScissor(buf, &scissor);
  gfxCmdPopScissor(buf);
  gfxCmdSetFgColor(buf, 0x11223344);
  gfxCmdSetBgColor(buf, 0xAABBCCDD);
  gfxCmdSetBlend(buf, GFX_BLEND_XOR);
  gfxCmdSetFont(buf, 0xF101);
  gfxCmdClear(buf);
  gfxCmdDrawRect(buf, &rect, true);
  gfxCmdDrawLine(buf, first, second);
  gfxCmdDrawTriangle(buf, first, second, third, false);
  gfxCmdDrawCircle(buf, center, 9, true);
  gfxCmdDrawImage(buf, 0x0102, second);
  gfxCmdDrawSubImage(buf, 0x0102, &rect, third);
  gfxCmdDrawText(buf, first, "hi");
  gfxCmdCall(buf, 0x0304);
  gfxCmdCopyRect(buf, &rect, third);
  gfxCmdScrollRect(buf, &rect, -1, 2);

  const uint8_t expected[] = {
    GFX_CMD_SET_VIEWPORT, 1, 0, 2, 0, 3, 0, 4, 0,
    GFX_CMD_SET_ORIGIN, 0xFF, 0xFF, 5, 0,
    GFX_CMD_PUSH_SCISSOR, 10, 0, 20, 0, 30, 0, 40, 0,
    GFX_CMD_POP_SCISSOR,
    GFX_CMD_SET_FG_COLOR, 0x44, 0x33, 0x22, 0x11,
    GFX_CMD_SET_BG_COLOR, 0xDD, 0xCC, 0xBB, 0xAA,
    GFX_CMD_SET_BLEND, GFX_BLEND_XOR,
    GFX_CMD_SET_FONT, 0x01, 0xF1,
    GFX_CMD_CLEAR,
    GFX_CMD_DRAW_RECT, 1, 0, 2, 0, 3, 0, 4, 0, 1,
    GFX_CMD_DRAW_LINE, 1, 0, 2, 0, 3, 0, 4, 0,
    GFX_CMD_DRAW_TRIANGLE, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 0,
    GFX_CMD_DRAW_CIRCLE, 7, 0, 8, 0, 9, 0, 1,
    GFX_CMD_DRAW_IMAGE, 0x02, 0x01, 3, 0, 4, 0,
    GFX_CMD_DRAW_SUB_IMAGE, 0x02, 0x01, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0,
    GFX_CMD_DRAW_TEXT, 1, 0, 2, 0, 2, 'h', 'i',
    GFX_CMD_CALL, 0x04, 0x03,
    GFX_CMD_COPY_RECT, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0,
    GFX_CMD_SCROLL_RECT, 1, 0, 2, 0, 3, 0, 4, 0, 0xFF, 0xFF, 2, 0,
  };
  const size_t sizes = GFX_CMD_SET_VIEWPORT_SIZE + GFX_CMD_SET_ORIGIN_SIZE
    + GFX_CMD_PUSH_SCISSOR_SIZE + GFX_CMD_POP_SCISSOR_SIZE
    + GFX_CMD_SET_FG_COLOR_SIZE + GFX_CMD_SET_BG_COLOR_SIZE
    + GFX_CMD_SET_BLEND_SIZE + GFX_CMD_SET_FONT_SIZE + GFX_CMD_CLEAR_SIZE
    + GFX_CMD_DRAW_RECT_SIZE + GFX_CMD_DRAW_LINE_SIZE
    + GFX_CMD_DRAW_TRIANGLE_SIZE + GFX_CMD_DRAW_CIRCLE_SIZE
    + GFX_CMD_DRAW_IMAGE_SIZE + GFX_CMD_DRAW_SUB_IMAGE_SIZE
    + GFX_CMD_DRAW_TEXT_SIZE(2) + GFX_CMD_CALL_SIZE + GFX_CMD_COPY_RECT_SIZE
    + GFX_CMD_SCROLL_RECT_SIZE;
  NANO_OS_ASSERT_EQ_INT((int) sizeof(expected), (int) sizes);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, buf->error);
  NANO_OS_ASSERT_EQ_INT((int) sizeof(expected), (int) buf->used);
  NANO_OS_ASSERT_TRUE(bytesEqual(expected, buf->data[buf->writeIdx],
    sizeof(expected)));
  gfxCmdBufDestroy(buf);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_record_that_doesnt_fit_goes_whole_into_the_next_submit,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 32);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, 0);
  for (int ii = 0; ii < 31; ii++) {
    gfxCmdPopScissor(buf);
  }
  GfxRect rect = { 1, 2, 3, 4 };
  gfxCmdDrawRect(buf, &rect, true);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  gfxCmdBufDestroy(buf);

  NANO_OS_ASSERT_EQ_INT(2, numCommandLists);
  NANO_OS_ASSERT_EQ_INT(31, commandSizes[0]);
  for (int ii = 0; ii < 31; ii++) {
    NANO_OS_ASSERT_EQ_INT(GFX_CMD_POP_SCISSOR, commands[0][ii]);
  }
  const uint8_t expected[] = {
    GFX_CMD_DRAW_RECT, 1, 0, 2, 0, 3, 0, 4, 0, 1,
  };
  NANO_OS_ASSERT_EQ_INT((int) sizeof(expected), commandSizes[1]);
  NANO_OS_ASSERT_TRUE(bytesEqual(expected, commands[1], sizeof(expected)));
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_record_larger_than_the_buffer_is_refused_whole,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 8);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, 0);
  GfxRect rect = { 1, 2, 3, 4 };
  gfxCmdDrawRect(buf, &rect, true);
  NANO_OS_ASSERT_EQ_INT(0, (int) buf->used);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(1, (int) buf->used);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_CMDBUF_OVERFLOW, gfxSubmit(buf));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  gfxCmdBufDestroy(buf);
  NANO_OS_ASSERT_EQ_INT(1, numCommandLists);
  NANO_OS_ASSERT_EQ_INT(1, commandSizes[0]);
  NANO_OS_ASSERT_EQ_INT(GFX_CMD_CLEAR, commands[0][0]);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, text_longer_than_the_maximum_records_nothing,
  graphicsPreBoot
) {
  GfxDisplay disp;
  memset(&disp, 0, sizeof(disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 512);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  char text[301];
  memset(text, 'x', 300);
  text[300] = '\0';
  GfxPoint pos = { 0, 0 };
  gfxCmdDrawText(buf, pos, text);
  NANO_OS_ASSERT_EQ_INT(0, (int) buf->used);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_CMDBUF_OVERFLOW, buf->error);

  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  text[GFX_TEXT_MAX] = '\0';
  gfxCmdDrawText(buf, pos, text);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, buf->error);
  NANO_OS_ASSERT_EQ_INT(GFX_CMD_DRAW_TEXT_SIZE(GFX_TEXT_MAX), (int) buf->used);
  NANO_OS_ASSERT_EQ_INT(GFX_TEXT_MAX, buf->data[buf->writeIdx][5]);
  NANO_OS_ASSERT_EQ_INT('x', buf->data[buf->writeIdx][6 + GFX_TEXT_MAX - 1]);
  gfxCmdBufDestroy(buf);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_missing_rectangle_records_nothing,
  graphicsPreBoot
) {
  GfxDisplay disp;
  memset(&disp, 0, sizeof(disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 64);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  GfxPoint pos = { 0, 0 };
  gfxCmdDrawSubImage(buf, 0x0102, NULL, pos);
  gfxCmdDrawRect(buf, NULL, true);
  NANO_OS_ASSERT_EQ_INT(0, (int) buf->used);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_INVALID_STATE, buf->error);
  gfxCmdBufDestroy(buf);
}

// --- submit and preupload -------------------------------------------------

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_submit_waits_in_wait_state_for_the_buffer_it_reuses,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  resetDriver(DRIVER_HOLD_SUBMITS);
  answerStatus = GFX_ERR_NOT_SUPPORTED;
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 32);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, 0);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  NANO_OS_ASSERT_FALSE(heldReleased);
  gfxCmdPopScissor(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_NOT_SUPPORTED, gfxSubmit(buf));
  NANO_OS_ASSERT_TRUE(heldReleased);
  NANO_OS_ASSERT_EQ_INT(PROCESS_STATE_WAIT, clientStateWhileHeld);
  NANO_OS_ASSERT_EQ_INT(2, numCommandLists);
  NANO_OS_ASSERT_EQ_INT(GFX_CMD_CLEAR, commands[0][0]);
  NANO_OS_ASSERT_EQ_INT(GFX_CMD_POP_SCISSOR, commands[1][0]);

  resetDriver(DRIVER_ANSWER);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  gfxCmdBufDestroy(buf);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, destroy_waits_for_the_buffer_in_flight,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  messageArgsResetCounts();
  uint16_t live = messageArgsCounts()->live;
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 32);
  NANO_OS_ASSERT_NOT_NULL(buf);
  resetDriver(DRIVER_HOLD_YIELDS);
  holdYields = 200;
  gfxCmdBufBegin(buf, 0);
  gfxCmdClear(buf);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  NANO_OS_ASSERT_NULL(gfxCmdBufDestroy(buf));
  NANO_OS_ASSERT_EQ_INT(PROCESS_STATE_WAIT, clientStateWhileHeld);
  NANO_OS_ASSERT_EQ_INT(live, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_preupload_sends_the_whole_recording,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 16);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  GfxRect rect = { 1, 2, 3, 4 };
  gfxCmdDrawRect(buf, &rect, false);
  gfxCmdClear(buf);
  gfxCmdPopScissor(buf);
  GfxHandle handle = GFX_INVALID_HANDLE;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxPreupload(buf, &handle));
  NANO_OS_ASSERT_EQ_INT(FIRST_HANDLE, handle);
  NANO_OS_ASSERT_EQ_INT(0, (int) buf->used);
  gfxCmdBufDestroy(buf);
  const uint8_t expected[] = {
    GFX_CMD_DRAW_RECT, 1, 0, 2, 0, 3, 0, 4, 0, 0,
    GFX_CMD_CLEAR, GFX_CMD_POP_SCISSOR,
  };
  NANO_OS_ASSERT_EQ_INT(1, numReceived);
  NANO_OS_ASSERT_TRUE(received[0].type == gfxType(GFX_MSG_PREUPLOAD));
  NANO_OS_ASSERT_EQ_INT((int) sizeof(expected), commandSizes[0]);
  NANO_OS_ASSERT_TRUE(bytesEqual(expected, commands[0], sizeof(expected)));
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_preupload_too_long_for_the_buffer_executes_nothing,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  GfxCmdBuf *buf = gfxCmdBufCreate(&disp, 16);
  NANO_OS_ASSERT_NOT_NULL(buf);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  GfxRect rect = { 1, 2, 3, 4 };
  gfxCmdDrawRect(buf, &rect, false);
  gfxCmdDrawRect(buf, &rect, false);
  gfxCmdClear(buf);
  GfxHandle handle = 0x5555;
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_CMDBUF_OVERFLOW, gfxPreupload(buf, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_INVALID_HANDLE, handle);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSubmit(buf));
  gfxCmdBufDestroy(buf);
  for (int ii = 0; ii < 20; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_EQ_INT(0, numReceived);
}

// --- fonts and measurement ------------------------------------------------

NANO_OS_KERNEL_TEST_WITH(graphics_header, a_full_font_cache_refuses_an_upload,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  uint8_t glyphs[8] = { 0 };
  GfxFontDesc font = { 4, 8, 'A', 1, glyphs, sizeof(glyphs) };
  GfxHandle handle = GFX_INVALID_HANDLE;
  for (int ii = 0; ii < GFX_MAX_FONTS; ii++) {
    NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &font, &handle));
    NANO_OS_ASSERT_EQ_INT(FIRST_HANDLE + ii, handle);
    NANO_OS_ASSERT_EQ_INT(4, gfxMeasureText(&disp, handle, "x"));
  }
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_OUT_OF_HANDLES,
    gfxUploadFont(&disp, &font, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_INVALID_HANDLE, handle);
  NANO_OS_ASSERT_EQ_INT(GFX_MAX_FONTS, numReceived);
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, the_default_font_cant_be_destroyed,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  uint8_t glyphs[8] = { 0 };
  GfxFontDesc font = { 4, 10, 'A', 1, glyphs, sizeof(glyphs) };
  GfxHandle handle = GFX_INVALID_HANDLE;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &font, &handle));
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetDefaultFont(&disp, handle));
  uint8_t width = 0, height = 0;
  gfxGetGlyphSize(&disp, GFX_INVALID_HANDLE, &width, &height);
  NANO_OS_ASSERT_EQ_INT(4, width);
  NANO_OS_ASSERT_EQ_INT(10, height);
  NANO_OS_ASSERT_EQ_INT(GFX_ERR_INVALID_STATE, gfxDestroyFont(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(2, numReceived);
  NANO_OS_ASSERT_EQ_INT(4, gfxMeasureText(&disp, handle, "x"));

  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxSetDefaultFont(&disp, GFX_INVALID_HANDLE));
  gfxGetGlyphSize(&disp, GFX_INVALID_HANDLE, &width, &height);
  NANO_OS_ASSERT_EQ_INT(6, width);
  NANO_OS_ASSERT_EQ_INT(8, height);
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxDestroyFont(&disp, handle));
  NANO_OS_ASSERT_EQ_INT(0, gfxMeasureText(&disp, handle, "x"));
}

NANO_OS_KERNEL_TEST_WITH(graphics_header, measurements_of_long_strings_saturate,
  graphicsPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver(DRIVER_ANSWER));
  GfxDisplay disp;
  NANO_OS_ASSERT_TRUE(openDisplay(&disp));
  uint8_t glyphs[8] = { 0 };
  GfxFontDesc narrow = { 1, 1, 'A', 1, glyphs, sizeof(glyphs) };
  GfxHandle handle = GFX_INVALID_HANDLE;
  NANO_OS_ASSERT_EQ_INT(GFX_OK, gfxUploadFont(&disp, &narrow, &handle));

  size_t length = sizeof(longText) - 1;
  char *text = longText;
  memset(text, 'x', length);
  text[length] = '\0';
  NANO_OS_ASSERT_EQ_INT(UINT16_MAX,
    gfxMeasureText(&disp, GFX_INVALID_HANDLE, text));
  NANO_OS_ASSERT_EQ_INT(UINT16_MAX, gfxHitTestText(&disp, handle, text,
    UINT16_MAX));
  NANO_OS_ASSERT_EQ_INT(UINT16_MAX, gfxFitText(&disp, handle, text,
    UINT16_MAX));
  uint16_t width = 0, height = 0;
  NANO_OS_ASSERT_TRUE(gfxMeasureTextSize(&disp, GFX_INVALID_HANDLE, text,
    &width, &height));
  NANO_OS_ASSERT_EQ_INT(UINT16_MAX, width);
  NANO_OS_ASSERT_EQ_INT(8, height);
  for (size_t ii = 1; ii < length; ii += 2) {
    text[ii] = '\n';
  }
  NANO_OS_ASSERT_TRUE(gfxMeasureTextSize(&disp, GFX_INVALID_HANDLE, text,
    &width, &height));
  NANO_OS_ASSERT_EQ_INT(6, width);
  NANO_OS_ASSERT_EQ_INT(UINT16_MAX, height);

  NANO_OS_ASSERT_EQ_INT(18, gfxMeasureText(&disp, GFX_INVALID_HANDLE, "abc"));
  NANO_OS_ASSERT_EQ_INT(2, gfxHitTestText(&disp, GFX_INVALID_HANDLE, "abc",
    9));
  NANO_OS_ASSERT_EQ_INT(1, gfxFitText(&disp, GFX_INVALID_HANDLE, "abc", 11));
}
