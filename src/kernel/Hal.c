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

/// @fn bool currentProcessHasHalCapability(
///   HalSubsystem subsystem, uint32_t function)
///
/// @brief Find an IpcCapability object in the currently-running process's
/// halCapabilities array given a subsystem and function.
///
/// @param subsystem The HalSubsystem to search for.
/// @param function A Hal*Function enum value within the subsystem to search
///   for.
///
/// @return Returns true if the capability is found in the current process's
/// halCapabilities, false if not.
bool currentProcessHasHalCapability(
  HalSubsystem subsystem, uint32_t function
) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // Unlikely but possible in the very early stages of booting.
    return false;
  }

  return (findHalCapability(
    processDescriptor->halCapabilities, processDescriptor->numHalCapabilities,
    subsystem, function));
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

