//----------------------------------------------------------------------------
// Shared routines for formatting and printing debug output from the Hone
// driver
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

#ifndef DBGPRINT_H
#define DBGPRINT_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if DBG

enum DebugLevel {
	D_ERR  = 0, // Error messages
	D_WARN,     // Warning messages
	D_INFO,     // Informational messages
	D_DBG,      // Verbose debugging messages
	D_LOCK,     // Verbose lock debugging messages
};

#define DBGPRINT(level, format, ...) \
{ \
	char  timestamp[32]; \
	char *levelStr; \
	FormatTimestamp(timestamp, sizeof(timestamp)); \
	switch (level) { \
	case D_ERR:  levelStr = "ERR "; break; \
	case D_WARN: levelStr = "WARN"; break; \
	case D_DBG:  levelStr = "DBG "; break; \
	case D_LOCK: levelStr = "LOCK"; break; \
	default:     levelStr = "INFO"; break; \
	} \
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, level, "HONE %s %s %s: " format "\n", \
			levelStr, timestamp, __FUNCTION__, ## __VA_ARGS__); \
}

#define BREAKPOINT() __debugbreak()

//----------------------------------------------------------------------------
/// @brief Copies a formatted timestamp into the buffer
///
/// @param buffer  Buffer to receive the timestamp
/// @param size    Size of the buffer
void FormatTimestamp(__in char *buffer, __in const size_t size);

#else // DBG

#define DBGPRINT(...)
#define BREAKPOINT()

#endif // DBG

#ifdef __cplusplus
};      // extern "C"
#endif

#endif // DBGPRINT_H
