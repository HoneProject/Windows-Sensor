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

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "system_id.h"
#include "debug_print.h"

#ifdef __cplusplus
extern "C" {
#endif

static wchar_t *gSystemIdValueName = L"SystemId";
static wchar_t *gSystemIdKeyRoot   = L"\\Registry\\Machine\\SOFTWARE\\PNNL";
static wchar_t *gSystemIdKeyPath   = L"\\Registry\\Machine\\SOFTWARE\\PNNL\\Hone";

//----------------------------------------------------------------------------
NTSTATUS SystemIdQueryRoutine(
	__in wchar_t       *valueName,
	__in unsigned long  valueType,
	__in void          *valueData,
	__in unsigned long  valueLength,
	__in void          *context,
	__in void          *entryContext)
{
	UNREFERENCED_PARAMETER(context);
	if (valueName && valueData && entryContext &&
			(0 == wcscmp(valueName, gSystemIdValueName)) &&
			(valueType == REG_BINARY) && (valueLength >= sizeof(GUID))) {
		RtlCopyMemory(entryContext, valueData, sizeof(GUID));
		return STATUS_SUCCESS;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS GetSystemId(__in GUID *systemId)
{
	NTSTATUS                  status;
	RTL_QUERY_REGISTRY_TABLE  queryTable[2] = {0};

	RtlZeroMemory(systemId, sizeof(GUID));

	// Create the registry key if necessary
	// Returns STATUS_SUCCESS if key exists or is created successfully
	status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, gSystemIdKeyRoot);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_WARN, "Cannot create registry key root: %08X", status);
		return status;
	}
	status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, gSystemIdKeyPath);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_WARN, "Cannot create registry key: %08X", status);
		return status;
	}

	// Get the system ID if possible
	queryTable[0].QueryRoutine = SystemIdQueryRoutine;
	queryTable[0].Flags        = RTL_QUERY_REGISTRY_REQUIRED;
	queryTable[0].Name         = gSystemIdValueName;
	queryTable[0].EntryContext = systemId;
	queryTable[0].DefaultType  = REG_NONE;

	status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, gSystemIdKeyPath,
			queryTable, NULL, NULL);
	if (status == STATUS_OBJECT_NAME_NOT_FOUND) {
		// Create a new ID
		do {
			status = ExUuidCreate(systemId);
		} while (status == STATUS_RETRY);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_WARN, "Cannot create system ID: %08X", status);
			return status;
		}
		status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, gSystemIdKeyPath,
				gSystemIdValueName, REG_BINARY, systemId, sizeof(GUID));
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_WARN, "Cannot write registry value: %08X", status);
			return status;
		}
	} else if (!NT_SUCCESS(status)) {
		DBGPRINT(D_WARN, "Cannot query registry value: %08X", status);
		return status;
	}

	DBGPRINT(D_INFO, "System ID is {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
			systemId->Data1, systemId->Data2, systemId->Data3,
			systemId->Data4[0], systemId->Data4[1], systemId->Data4[2], systemId->Data4[3],
			systemId->Data4[4], systemId->Data4[5], systemId->Data4[6], systemId->Data4[7]);
	return status;
}

#ifdef __cplusplus
}; // extern "C"
#endif
