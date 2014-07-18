//----------------------------------------------------------------------------
// Hone user-mode utility common functions
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------

#include <Windows.h>
#include <stdio.h>

#include "common.h"

//--------------------------------------------------------------------------
void LogError(const UINT32 errorCode)
{
	char *buffer;

	if (FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			errorCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR) &buffer,
			0, NULL )) {
		printf("%d - %s", errorCode, buffer);
		LocalFree(buffer);
	}
}

//--------------------------------------------------------------------------
void LogError(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	fputs(": ", stdout);
	LogError(GetLastError());
}

//--------------------------------------------------------------------------
void LogError(const UINT32 errorCode, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	fputs(": ", stdout);
	LogError(errorCode);
}

//--------------------------------------------------------------------------
HANDLE OpenDriver(const bool verbose)
{
	HANDLE      driver     = INVALID_HANDLE_VALUE;
	const char *driverFile = "\\\\.\\HoneOut";

	driver = CreateFile(driverFile, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, 0);
	if (driver == INVALID_HANDLE_VALUE) {
		LogError("Cannot open driver %s", driverFile);
	} else if (verbose) {
		printf("Opened %s\n", driverFile);
	}
	return driver;
}
