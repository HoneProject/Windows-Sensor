//----------------------------------------------------------------------------
// Collects process information for the Hone driver
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

#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

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
/// @brief Deinitializes the process monitor
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS DeinitializeProcessMonitor(void);

//----------------------------------------------------------------------------
/// @brief Initializes the process monitor
///
/// @param device  WDM device object for this driver
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS InitializeProcessMonitor(__in DEVICE_OBJECT *device);

#ifdef __cplusplus
}; // extern "C"
#endif

#endif	// PROCESS_MONITOR_H
