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

// Custom includes
#include "src/hal/HalAdafruitFeatherM0Wifi.h"
#include "src/hal/HalArduinoNano33Iot.h"
#include "src/hal/HalArduinoNanoEvery.h"
#include "src/kernel/NanoOs.h"
#include "src/kernel/Scheduler.h"
#include "src/kernel/SdCardSpi.h"
#include "src/user/NanoOsLibC.h"
#include "src/user/NanoOsStdio.h"

const Hal *HAL = NULL;

// The setup function runs once when you press reset or power the board.  This
// is to be used for Arduino-specific setup.  *ANYTHING* that requires use of
// coroutines needs to be done in the loop function.
void setup() {
#if defined(ADAFRUIT_FEATHER_M0)
  HAL = halAdafruitFeatherM0WifiInit();
#elif defined(ARDUINO_SAMD_NANO_33_IOT)
  HAL = halArduinoNano33IotInit();
#elif defined(ARDUINO_AVR_NANO_EVERY)
  HAL = halArduinoNanoEveryInit();
#endif

  if (HAL == NULL) {
    // Nothing we can do.  Halt.
    while(1);
  }
}

// In a normal Arduino sketch, the loop function runs over and over again
// forever.  For NanoOs, it will be called once and never exit.  This is because
// we want to do everything related to coroutines in this stack.  The reason
// we want to do this is because we want to hide as much of the coroutine
// metadata storage as we can within the stack of the main loop.  Every stack,
// including the stack for the main loop, is HAL->processStackSize() bytes in
// size.  If we declare no variables in this stack, we're just wasting space.
// Also, making the storage for the metadata global consumes precious memory.
// It's much more efficient to just store the pointers in the global address
// space and put the real storage in the main loop's stack.  However, that
// means that we have to do all the one-time setup from within the main
// function.  That, in turn, means that we can never exit this function.  So,
// we will do all the one-time setup and then run our scheduler loop from
// within this call.
void loop() {
  // SchedulerState pointer that we will have to populate in startScheduler.
  SchedulerState *coroutineStatePointer = NULL;

  // We want the address of the first coroutine to be as close to the base as
  // possible.  Because of that, we need to create the first one before we enter
  // the scheduler.  That means we need to allocate the main coroutine here,
  // configure it, and then create and run one before we ever enter the
  // scheduler.
  Coroutine _mainCoroutine;
  schedulerTaskHandle = &_mainCoroutine;
  CoroutineConfigOptions coroutineConfigOptions = {
    .stackSize = HAL->processStackSize(),
    .stateData = &coroutineStatePointer,
    .coroutineYieldCallback = NULL,
    .comutexUnlockCallback = comutexUnlockCallback,
    .coconditionSignalCallback = coconditionSignalCallback,
  };
  if (HAL->getNumTimers() > 0) {
    coroutineConfigOptions.coroutineYieldCallback = coroutineYieldCallback;
  }
  if (coroutineConfig(&_mainCoroutine, &coroutineConfigOptions)
    != coroutineSuccess
  ) {
    printString("coroutineConfig failed.\n");
    while(1);
  }
  // Create but *DO NOT* resume one dummy task.  This will set the size of
  // the main stack.
  if (coroutineInit(NULL, dummyTask, NULL) == NULL) {
    printString("Could not set scheduler task's stack size.\n");
  }

  // Enter the scheduler.  This never returns.
  printDebugString("Starting scheduler.\n");
  startScheduler(&coroutineStatePointer);
}

