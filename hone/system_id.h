//----------------------------------------------------------------------------
// Gets the system GUID for the Hone installation
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef SYSTEM_ID_H
#define SYSTEM_ID_H

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
/// @brief Gets the system GUID for the Hone installation
///
/// @param systemId  Buffer to receive the system GUID
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS GetSystemId(__in GUID *systemId);

//----------------------------------------------------------------------------
/// @brief Checks if the registry value is a valid system ID value
///
/// @param valueName     Name of the registry value
/// @param valueType     Type of the registry value
/// @param valueData     Data in the registry value
/// @param valueLength   Length of the data
/// @param context       Unused
/// @param entryContext  Destination buffer for the data
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
RTL_QUERY_REGISTRY_ROUTINE SystemIdQueryRoutine;

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // SYSTEM_ID_H
