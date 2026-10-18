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

/// @file Hal.c
///
/// @brief Common support functions for the HAL infrastructure.

// Standard C includes
#include "stddef.h"

// NanoOs includes
#include "Hal.h"
#include "Processes.h"

_Static_assert(HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS + HAL_UART_NUM_FNS
  + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS + HAL_CLOCK_NUM_FNS + HAL_POWER_NUM_FNS
  + HAL_TIMER_NUM_FNS + HAL_BLOCK_DEVICE_NUM_FNS
  <= 8 * HAL_CAPABILITY_BITMAP_SIZE,
  "HAL_CAPABILITY_BITMAP_SIZE is too small for every HAL function");
_Static_assert(HAL_DEVICE_MASKS_SIZE == HAL_NUM_SUBSYSTEMS,
  "HAL_DEVICE_MASKS_SIZE must match HAL_NUM_SUBSYSTEMS");

/// @var halCapabilityBitBase
///
/// @brief The index of each HalSubsystem's first function in a
/// halCapabilityBitmap, followed by the total number of HAL functions.  This
/// is deliberately not const so that it lives in .data like the capability
/// arrays rather than in .rodata or flash.
uint8_t halCapabilityBitBase[HAL_NUM_SUBSYSTEMS + 1] = {
  [HAL_PLATFORM]       = 0,
  [HAL_MEMORY]         = HAL_PLATFORM_NUM_FNS,
  [HAL_UART]           = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS,
  [HAL_DIO]            = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS,
  [HAL_SPI]            = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS,
  [HAL_CLOCK]          = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS,
  [HAL_POWER]          = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS
                         + HAL_CLOCK_NUM_FNS,
  [HAL_TIMER]          = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS
                         + HAL_CLOCK_NUM_FNS + HAL_POWER_NUM_FNS,
  [HAL_BLOCK_DEVICE]   = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS
                         + HAL_CLOCK_NUM_FNS + HAL_POWER_NUM_FNS
                         + HAL_TIMER_NUM_FNS,
  [HAL_NUM_SUBSYSTEMS] = HAL_PLATFORM_NUM_FNS + HAL_MEMORY_NUM_FNS
                         + HAL_UART_NUM_FNS + HAL_DIO_NUM_FNS + HAL_SPI_NUM_FNS
                         + HAL_CLOCK_NUM_FNS + HAL_POWER_NUM_FNS
                         + HAL_TIMER_NUM_FNS + HAL_BLOCK_DEVICE_NUM_FNS,
};

/// @fn void setProcessHalCapabilities(ProcessDescriptor *processDescriptor,
///   HalCapability *capabilities, size_t numCapabilities)
///
/// @brief Give a process a HalCapability array and build the bitmap and
/// device masks that callHal checks against it.  A subsystem's device mask
/// is the intersection of the deviceIds of its grants, so mixed masks fail
/// closed.
///
/// @param processDescriptor A pointer to the process's ProcessDescriptor.
/// @param capabilities A pointer to a sorted array of HalCapability objects.
///   May be NULL if numCapabilities is 0.
/// @param numCapabilities The number of HalCapability objects in the
///   capabilities array.
///
/// @return This function returns no value.
void setProcessHalCapabilities(ProcessDescriptor *processDescriptor,
  HalCapability *capabilities, size_t numCapabilities
) {
  processDescriptor->halCapabilities = capabilities;
  processDescriptor->numHalCapabilities = numCapabilities;
  uint8_t *bitmap = processDescriptor->halCapabilityBitmap;
  for (uint8_t ii = 0; ii < HAL_CAPABILITY_BITMAP_SIZE; ii++) {
    bitmap[ii] = 0;
  }
  uint8_t *deviceMasks = processDescriptor->halDeviceMasks;
  for (uint8_t ii = 0; ii < HAL_DEVICE_MASKS_SIZE; ii++) {
    deviceMasks[ii] = 0;
  }
  uint16_t seenSubsystems = 0;

  for (size_t ii = 0; ii < numCapabilities; ii++) {
    uint8_t subsystem = (uint8_t) (capabilities[ii].subsystemFunction >> 8);
    uint8_t function = (uint8_t) capabilities[ii].subsystemFunction;
    if ((subsystem >= HAL_NUM_SUBSYSTEMS)
      || (function >= (halCapabilityBitBase[subsystem + 1]
        - halCapabilityBitBase[subsystem]))
    ) {
      continue;
    }
    uint8_t bit = halCapabilityBitBase[subsystem] + function;
    bitmap[bit >> 3] |= (uint8_t) (1 << (bit & 7));
    if (seenSubsystems & (1 << subsystem)) {
      deviceMasks[subsystem] &= capabilities[ii].deviceIds;
    } else {
      deviceMasks[subsystem] = capabilities[ii].deviceIds;
      seenSubsystems |= (uint16_t) (1 << subsystem);
    }
  }
}

/// @fn HalCapability* findHalCapability(
///   HalCapability *capabilities, size_t numCapabilities,
///   HalSubsystem subsystem, uint32_t function)
///
/// @brief Find a HalCapability object in an array of them given a subsystem and
/// function.
///
/// @param capabilities An array of HalCapability objects.
/// @param numCapabilities The number of HalCapability objects in the
///   capabilities array.
/// @param subsystem The HalSubsystem to search for.
/// @param function A Hal*Function enum value within the subsystem to search
///   for.
///
/// @return Returns a pointer to the first matching capability on success, NULL
/// on failure.
HalCapability* findHalCapability(
  HalCapability *capabilities, size_t numCapabilities,
  HalSubsystem subsystem, uint32_t function
) {
  // The array of capabilites is expected to be small.  If we're running on a
  // CPU with cache prefetch, it may actually load the entire array into cache.
  // Just do a linear search with an early termination since the array is
  // sorted.
  uint16_t subsystemFunction = (((uint16_t) subsystem) << 8) | function;
  for (size_t ii = 0; ii < numCapabilities; ii++) {
    HalCapability *capability = &capabilities[ii];
    if (capability->subsystemFunction == subsystemFunction) {
      return capability;
    } else if (capability->subsystemFunction > subsystemFunction) {
      // We've passed the subsystem we're looking for in the array, so it's not
      // there.  Bail.
      return NULL;
    }
  }

  // We searched the entire array and found nothing.  Return NULL.
  return NULL;
}

/// @fn HalCapability* findHalCapabilityWithDevice(
///   HalCapability *capabilities, size_t numCapabilities,
///   HalSubsystem subsystem, uint32_t function, int32_t deviceId)
///
/// @brief Find a HalCapability object in an array of them given a subsystem,
/// function, and device ID.
///
/// @param capabilities An array of HalCapability objects.
/// @param numCapabilities The number of HalCapability objects in the
///   capabilities array.
/// @param subsystem The HalSubsystem to search for.
/// @param function A Hal*Function enum value within the subsystem to search
///   for.
/// @param deviceId The ID of the device within the subsystem function to search
///   for.
///
/// @return Returns a pointer to the matching capability on success, NULL on
/// failure.
HalCapability* findHalCapabilityWithDevice(
  HalCapability *capabilities, size_t numCapabilities,
  HalSubsystem subsystem, uint32_t function, int32_t deviceId
) {
  HalCapability *halCapability = findHalCapability(
    capabilities, numCapabilities, subsystem, function);
  if (halCapability != NULL) {
    if (halCapability->deviceIds & (1 << deviceId)) {
      return halCapability;
    }
  }

  // We searched the entire array and found nothing.  Return NULL.
  return NULL;
}

