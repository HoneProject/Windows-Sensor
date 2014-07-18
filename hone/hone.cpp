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

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "hone.h"

//----------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------

static const DRIVER_COMPONENT gComponents[] = {
	{"queue manager",   InitializeQueueManager,   DeinitializeQueueManager  },
	{"process monitor", InitializeProcessMonitor, DeinitializeProcessMonitor},
	{"network monitor", InitializeNetworkMonitor, DeinitializeNetworkMonitor},
	{"read interface",  InitializeReadInterface,  DeinitializeReadInterface },
};

static const UINT32 gPoolTag = 'enoH'; // Tag to use when allocating pool data

//----------------------------------------------------------------------------
void DeinitializeComponents(void)
{
	// Deinitialize components in reverse order they were initialized in
	for (int i = ARRAY_SIZEOF(gComponents) - 1; i >= 0; i--) {
		DBGPRINT(D_INFO, "Deinitializing %s", gComponents[i].Name);
		const NTSTATUS status = gComponents[i].Deinitialize();
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_WARN, "Cannot deinitialize %s", gComponents[i].Name);
		} else {
			DBGPRINT(D_INFO, "Finished deinitializing %s", gComponents[i].Name);
		}
	}
}

//----------------------------------------------------------------------------
__drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS DriverEntry(
	__in PDRIVER_OBJECT  driverObject,
	__in PUNICODE_STRING registryPath)
{
	NTSTATUS           status;
	WDF_DRIVER_CONFIG  wdfConfig;
	WDFDEVICE          wdfDevice;
	WDFDRIVER          wdfDriver;
	WDFDEVICE_INIT    *wdfInit    = NULL;
	DEVICE_OBJECT     *wdmDevice  = NULL;
	static const GUID  deviceGuid = {0x5728b2c2, 0x859, 0x4b9f,
			{0xa0, 0xdc, 0xb4, 0x12, 0xc4, 0x47, 0xe8, 0x10}};

	DECLARE_CONST_UNICODE_STRING(deviceName,     L"\\Device\\HoneOut");
	DECLARE_CONST_UNICODE_STRING(deviceLinkName, L"\\DosDevices\\HoneOut");

	DBGPRINT(D_INFO, "%s - Version %s, compiled %s %s",
			HONE_DESCRIPTION_STR, HONE_VERSION_STR, __DATE__, __TIME__);
	DBGPRINT(D_INFO, "Initializing driver");

	// Create WDF driver object
	WDF_DRIVER_CONFIG_INIT(&wdfConfig, WDF_NO_EVENT_CALLBACK);
	wdfConfig.DriverInitFlags |= WdfDriverInitNonPnpDriver;
	wdfConfig.DriverPoolTag    = gPoolTag;
	wdfConfig.EvtDriverUnload  = DriverUnload;
	status                     = WdfDriverCreate(driverObject, registryPath,
			WDF_NO_OBJECT_ATTRIBUTES, &wdfConfig, &wdfDriver);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create WDF driver: %08X", status);
		goto Cleanup;
	}

	// Create WDF device object
	wdfInit = WdfControlDeviceInitAllocate(wdfDriver, &SDDL_DEVOBJ_KERNEL_ONLY);
	if (!wdfInit) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		DBGPRINT(D_ERR, "Cannot allocate WDF device initialization structure: %08X",
				status);
		goto Cleanup;
	}
	WdfDeviceInitSetDeviceClass(wdfInit, &deviceGuid);
	WdfDeviceInitSetDeviceType(wdfInit, FILE_DEVICE_NETWORK);
	WdfDeviceInitSetCharacteristics(wdfInit, FILE_DEVICE_SECURE_OPEN, FALSE);
	status = WdfDeviceInitAssignName(wdfInit, &deviceName);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot assign name to WDF device: %08X", status);
		goto Cleanup;
	}
	status = WdfDeviceInitAssignSDDLString(wdfInit, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot assign security descriptor to WDF device: %08X", status);
		goto Cleanup;
	}
	status = WdfDeviceCreate(&wdfInit, WDF_NO_OBJECT_ATTRIBUTES, &wdfDevice);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create WDF device: %08X", status);
		goto Cleanup;
	}

	status = WdfDeviceCreateSymbolicLink(wdfDevice, &deviceLinkName);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create WDF symbolic link: %08X", status);
		goto Cleanup;
	}

	// Get the WDM device object
	WdfControlFinishInitializing(wdfDevice);
	wdmDevice = WdfDeviceWdmGetDeviceObject(wdfDevice);

	// Initialize components
	status = InitializeComponents(wdmDevice);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot initialize components: %08X", status);
		goto Cleanup;
	}

	// Finish initializing read interface
	driverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreate;
	driverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchClose;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
	driverObject->MajorFunction[IRP_MJ_READ]           = DispatchRead;

Cleanup:
	if (NT_SUCCESS(status)) {
		DBGPRINT(D_INFO, "Finished initializing driver");
	} else {
		// The framework doesn't call DriverUnload if DriverEntry returns an error
		// http://msdn.microsoft.com/en-us/library/ff541694.aspx
		DeinitializeComponents();
		DBGPRINT(D_ERR, "Failed to initialize driver");
	}
	return status;
}

//----------------------------------------------------------------------------
void DriverUnload(__in WDFDRIVER driverObject)
{
	UNREFERENCED_PARAMETER(driverObject);
	DBGPRINT(D_INFO, "Unloading driver");
	DeinitializeComponents();
	DBGPRINT(D_INFO, "Finished unloading driver");
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS InitializeComponents(__in DEVICE_OBJECT *device)
{
	for (int i = 0; i < ARRAY_SIZEOF(gComponents); i++) {
		DBGPRINT(D_INFO, "Initializing %s", gComponents[i].Name);
		const NTSTATUS status = gComponents[i].Initialize(device);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot initialize %s", gComponents[i].Name);
			return status;
		}
		DBGPRINT(D_INFO, "Finished initializing %s", gComponents[i].Name);
	}
	return STATUS_SUCCESS;
}

#ifdef __cplusplus
};
#endif
