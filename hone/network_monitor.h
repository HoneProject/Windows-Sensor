//----------------------------------------------------------------------------
// Collects connection and packet information using Windows Filter Platform
// for the Hone driver
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Alexis J. Malozemoff <alexis.malozemoff@pnnl.gov>
//   Peter L. Nordquist <peter.nordquist@pnnl.gov>
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//   Ruslan A. Doroshchuk <ruslan.doroshchuk@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Deinitializes the network monitor
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS DeinitializeNetworkMonitor(void);

//----------------------------------------------------------------------------
/// @brief Initializes the network monitor
///
/// @param device  WDM device object for this driver
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS InitializeNetworkMonitor(__in DEVICE_OBJECT *device);

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // NETWORK_MONITOR_H
