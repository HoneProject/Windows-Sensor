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

#ifdef DBG

#include "debug_print.h"
#include <ntstrsafe.h>

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
void FormatTimestamp(__in char *buffer, __in const size_t size)
{
	LARGE_INTEGER localTime;
	LARGE_INTEGER systemTime;
	TIME_FIELDS   timeFields;

	KeQuerySystemTime(&systemTime);
	ExSystemTimeToLocalTime(&systemTime, &localTime);
	RtlTimeToTimeFields(&localTime, &timeFields);
	RtlStringCbPrintfA(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
			timeFields.Year, timeFields.Month, timeFields.Day,
			timeFields.Hour, timeFields.Minute, timeFields.Second, timeFields.Milliseconds);
}

#ifdef __cplusplus
};      // extern "C"
#endif

#endif // DBG
