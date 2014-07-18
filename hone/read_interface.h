//----------------------------------------------------------------------------
// Provides an interface for userspace programs to read PCAP-NG blocks
// collected by the Hone driver
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

#ifndef READ_INTERFACE_H
#define READ_INTERFACE_H

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
/// @brief Deinitializes the read interface
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS DeinitializeReadInterface(void);

//----------------------------------------------------------------------------
/// @brief Closes an open device
///
/// @param deviceObject  The target device for the operation
/// @param irp           I/O request packet for the operation
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH DispatchClose;

//----------------------------------------------------------------------------
/// @brief Creates a new device or opens an existing device
///
/// @param deviceObject  The target device for the operation
/// @param irp           I/O request packet for the operation
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__drv_dispatchType(IRP_MJ_CREATE) DRIVER_DISPATCH DispatchCreate;

//----------------------------------------------------------------------------
/// @brief Handles device I/O control commands
///
/// @param deviceObject  The target device for the operation
/// @param irp           I/O request packet for the operation
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH DispatchDeviceControl;

//----------------------------------------------------------------------------
/// @brief Reads data from the device
///
/// @param deviceObject  The target device for the operation
/// @param irp           I/O request packet for the operation
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__drv_dispatchType(IRP_MJ_READ) DRIVER_DISPATCH DispatchRead;

//----------------------------------------------------------------------------
/// @brief Initializes the read interface
///
/// @param device  WDM device object for this driver
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS InitializeReadInterface(__in DEVICE_OBJECT *device);

#ifdef __cplusplus
};
#endif

#endif // READ_INTERFACE_H
