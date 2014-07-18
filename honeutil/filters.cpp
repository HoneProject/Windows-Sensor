//-----------------------------------------------------------------------------
// Hone user-mode utility filter operations
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------

#include <Windows.h>
#include <fwpmu.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "filters.h"
#include "../wfp_common.h"

//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------

// Sublayer key
DEFINE_GUID(gSubLayerKey, 0xd75dc3e6, 0xee8d, 0x4de0, 0xb7, 0x28, 0xa7, 0x60,
	0x3a, 0xe5, 0x44, 0xa9);

static const size_t gBufferLen = 64;

//-----------------------------------------------------------------------------
DWORD AddCallout(HANDLE engineHandle, const int index)
{
	FWPM_CALLOUT      callout = {0};
	wchar_t           calloutName[gBufferLen];
	const LAYER_INFO *layer = HoneLayerInfo(index);
	wchar_t           layerName[gBufferLen];
	size_t            numChars;

	mbstowcs_s(&numChars, layerName, gBufferLen, layer->LayerName, _TRUNCATE);
	_snwprintf_s(calloutName, gBufferLen, _TRUNCATE, L"Hone %s callout",
			layerName);

	callout.calloutKey       = *layer->CalloutKey;
	callout.displayData.name = calloutName;
	callout.flags            = FWPM_CALLOUT_FLAG_PERSISTENT;
	callout.applicableLayer  = *layer->LayerKey;
	return FwpmCalloutAdd(engineHandle, &callout, NULL, NULL);
}

//-----------------------------------------------------------------------------
DWORD AddFilter(HANDLE engineHandle, const int index, const int boottime)
{
	FWPM_FILTER       filter = {0};
	wchar_t           filterName[gBufferLen];
	const LAYER_INFO *layer = HoneLayerInfo(index);
	wchar_t           layerName[gBufferLen];
	size_t            numChars;

	mbstowcs_s(&numChars, layerName, gBufferLen, layer->LayerName, _TRUNCATE);
	if (boottime) {
		_snwprintf_s(filterName, gBufferLen, _TRUNCATE,
				L"Hone %s boot-time filter", layerName);
		filter.flags     = FWPM_FILTER_FLAG_BOOTTIME;
		filter.filterKey = *layer->BootFilterKey;
	} else {
		_snwprintf_s(filterName, gBufferLen, _TRUNCATE,
				L"Hone %s persistent filter", layerName);
		filter.flags     = FWPM_FILTER_FLAG_PERSISTENT;
		filter.filterKey = *layer->FilterKey;
	}

	// Use unknown or terminating callouts, if the callouts modify packet data
	// Otherwise, use inspection callouts
	// http://msdn.microsoft.com/en-us/library/ff570963.aspx
	// http://msdn.microsoft.com/en-us/library/aa364248.aspx
	filter.displayData.name  = filterName;
	filter.action.type       = FWP_ACTION_CALLOUT_INSPECTION;
	filter.action.calloutKey = *layer->CalloutKey;
	filter.layerKey          = *layer->LayerKey;
	filter.subLayerKey       = gSubLayerKey;
	filter.weight.type       = FWP_EMPTY; // Auto-weight
	return FwpmFilterAdd(engineHandle, &filter, NULL, NULL);
}

//-----------------------------------------------------------------------------
DWORD AddSubLayer(HANDLE engineHandle)
{
	FWPM_SUBLAYER subLayer = {0};

	// The sublayer weight must be less than the weight of
	// FWPM_SUBLAYER_UNIVERSAL to be compatible with IPsec
	// http://msdn.microsoft.com/en-us/library/ff546423.aspx
	subLayer.subLayerKey      = gSubLayerKey;
	subLayer.displayData.name = L"Hone sub-Layer";
	subLayer.flags            = FWPM_SUBLAYER_FLAG_PERSISTENT;
	subLayer.weight           = 0;
	return FwpmSubLayerAdd(engineHandle, &subLayer, NULL);
}

//-----------------------------------------------------------------------------
DWORD DeleteCallout(HANDLE engineHandle, const int index)
{
	return FwpmCalloutDeleteByKey(engineHandle, HoneLayerInfo(index)->CalloutKey);
}

//-----------------------------------------------------------------------------
DWORD DeleteFilter(HANDLE engineHandle, const int index, const int boottime)
{
	const LAYER_INFO *layer = HoneLayerInfo(index);

	return FwpmFilterDeleteByKey(engineHandle, boottime ?
			layer->BootFilterKey : layer->FilterKey);
}

//-----------------------------------------------------------------------------
DWORD DeleteSubLayer(HANDLE engineHandle)
{
	return FwpmSubLayerDeleteByKey(engineHandle, &gSubLayerKey);
}

//-----------------------------------------------------------------------------
void LogDots(int chars)
{
	for (; chars < 51; chars++) {
		fputc((chars % 2) ? '.' : ' ', stdout);
	}
}

//-----------------------------------------------------------------------------
void LogHelper(const char *msg, const char *format, ...)
{
	va_list args;

	LogDots(printf("%s ", msg));

	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

//-----------------------------------------------------------------------------
void LogHelper(
	const char *layerName,
	const char *op,
	const char *type,
	const char *format,
	...)
{
	va_list args;

	if (layerName[0] == '\0') {
		LogDots(printf("%s %s ", op, type));
	} else {
		LogDots(printf("%s %s %s ", op, layerName, type));
	}

	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

//-----------------------------------------------------------------------------
DWORD Log(const bool verbose, const DWORD rc, const char *msg)
{
	if (rc) {
		LogHelper(msg, "failed %08X\n", rc);
	} else if (verbose) {
		LogHelper(msg, "success\n");
	}
	return rc;
}

//-----------------------------------------------------------------------------
DWORD Log(
	const bool   verbose,
	const DWORD  rc,
	const char  *op,
	const char  *type,
	const char  *layerName = "")
{
	if (rc) {
		switch (rc) {
		case FWP_E_ALREADY_EXISTS:
			if (verbose) {
				LogHelper(layerName, op, type, "already exists\n");
			}
			return ERROR_SUCCESS;
		case FWP_E_CALLOUT_NOT_FOUND:
		case FWP_E_FILTER_NOT_FOUND:
		case FWP_E_SUBLAYER_NOT_FOUND:
			if (verbose) {
				LogHelper(layerName, op, type, "does not exist\n");
			}
			return ERROR_SUCCESS;
		default:
			LogHelper(layerName, op, type, "failed %08X\n", rc);
		}
	} else if (verbose) {
		LogHelper(layerName, op, type, "success\n");
	}
	return rc;
}

//-----------------------------------------------------------------------------
bool SetupFilters(const bool verbose, const bool install)
{
	DWORD  rc            = ERROR_SUCCESS;
	HANDLE engineHandle  = NULL;   // Packet filter engine handle
	int    index;
	bool   inTransaction = false;

	rc = FwpmEngineOpen(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &engineHandle);
	if (Log(verbose, rc, "Open filter engine")) {
		goto Cleanup;
	}

	rc = FwpmTransactionBegin(engineHandle, 0);
	if (Log(verbose, rc, "Start transaction")) {
		goto Cleanup;
	}
	inTransaction = true;

	if (install) {
		rc = AddSubLayer(engineHandle);
		if (Log(verbose, rc, "Add", "sublayer")) {
			goto Cleanup;
		}

		// Add callouts and filters for each layer
		for (index = 0; index < HoneNumLayers(); index++) {
			rc = AddCallout(engineHandle, index);
			if (Log(verbose, rc, "Add", "callout",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}

			rc = AddFilter(engineHandle, index, true);
			if (Log(verbose, rc, "Add", "boot-time filter",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}

			rc = AddFilter(engineHandle, index, false);
			if (Log(verbose, rc, "Add", "persistent filter",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}
		}
	} else {
		// Delete callouts and filters for each layer
		for (index = 0; index < HoneNumLayers(); index++) {
			rc = DeleteFilter(engineHandle, index, false);
			if (Log(verbose, rc, "Delete", "persistent filter",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}

			rc = DeleteFilter(engineHandle, index, true);
			if (Log(verbose, rc, "Delete", "boot-time filter",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}

			rc = DeleteCallout(engineHandle, index);
			if (Log(verbose, rc, "Delete", "callout",
					HoneLayerInfo(index)->LayerName)) {
				goto Cleanup;
			}
		}

		rc = DeleteSubLayer(engineHandle);
		if (Log(verbose, rc, "Delete", "sublayer")) {
			goto Cleanup;
		}
		rc = ERROR_SUCCESS;
	}

Cleanup:

	if (engineHandle) {
		if (inTransaction) {
			if (rc != ERROR_SUCCESS) {
				rc = FwpmTransactionAbort(engineHandle);
				if (Log(verbose, rc, "Abort transaction")) {
					goto Cleanup;
				}
			} else {
				rc = FwpmTransactionCommit(engineHandle);
				if (Log(verbose, rc, "Commit transaction")) {
					goto Cleanup;
				}
			}
		}

		rc = FwpmEngineClose(engineHandle);
		if (Log(verbose, rc, "Close filter engine")) {
			goto Cleanup;
		}
	}

	return (rc == ERROR_SUCCESS) ? true : false;
}
