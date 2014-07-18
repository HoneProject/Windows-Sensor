//----------------------------------------------------------------------------
// Hone driver entry point
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

#ifndef HONE_H
#define HONE_H

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "queue_manager.h"
#include "network_monitor.h"
#include "process_monitor.h"
#include "read_interface.h"

#include <wdf.h>

#include "hone_info.h"
#include "debug_print.h"

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

typedef NTSTATUS (*INIT_FUNC)(__in DEVICE_OBJECT *device);
typedef NTSTATUS (*DEINIT_FUNC)(void);

struct DRIVER_COMPONENT {
	char        *Name;
	INIT_FUNC    Initialize;
	DEINIT_FUNC  Deinitialize;
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Deinitializes driver components
void DeinitializeComponents(__in void);

//----------------------------------------------------------------------------
/// @brief Initializes driver when driver is loaded
///
/// @param driverObject  This driver's object
/// @param registryPath  Path to the driver's registry keys
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
DRIVER_INITIALIZE DriverEntry;

//----------------------------------------------------------------------------
/// @brief Cleans up driver resources when driver is loaded
///
/// @param driverObject  This driver's object
EVT_WDF_DRIVER_UNLOAD DriverUnload;

//----------------------------------------------------------------------------
/// @brief Initializes driver components
///
/// @param device  WDM device object for this driver
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS InitializeComponents(__in DEVICE_OBJECT *device);


#ifdef __cplusplus
};	// extern "C"
#endif

#endif	// PROCMON_H
