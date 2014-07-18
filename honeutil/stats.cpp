//----------------------------------------------------------------------------
// Hone user-mode utility statistics operations
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
#include <WinIoCtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "common.h"
#include "stats.h"
#include "../ioctls.h"

//--------------------------------------------------------------------------
// Structures and enumerations
//--------------------------------------------------------------------------

struct TIME
{
	UINT16 Days;
	UINT8  Hours;
	UINT8  Minutes;
	UINT8  Seconds;
};

//--------------------------------------------------------------------------
void SecondsToTime(UINT32 seconds, TIME &time)
{
	time.Days    = static_cast<UINT16>(seconds / 86400);
	seconds     -= time.Days * 86400;
	time.Hours   = static_cast<UINT8>(seconds / 3600);
	seconds     -= time.Hours * 3600;
	time.Minutes = static_cast<UINT8>(seconds / 60);
	seconds     -= time.Minutes * 60;
	time.Seconds = static_cast<UINT8>(seconds);
}

//--------------------------------------------------------------------------
bool GetStatistics(const bool verbose, UINT32 snapLen)
{
	bool        rc = false;
	DWORD       bytesReturned;
	HANDLE      driver;
	STATISTICS  statistics;
	TIME        loadedTime;
	TIME        loggingTime;

	driver = OpenDriver(verbose);
	if (driver == INVALID_HANDLE_VALUE) {
		goto Cleanup;
	}

	if (snapLen) {
		if (!DeviceIoControl(driver, IOCTL_HONE_SET_SNAP_LENGTH, &snapLen,
				sizeof(UINT32), NULL, 0, &bytesReturned, NULL)) {
			LogError("Cannot send IOCTL to set snap length");
			goto Cleanup;
		}
	}

	if (!DeviceIoControl(driver, IOCTL_HONE_GET_STATISTICS, NULL, 0,
			&statistics, sizeof(statistics), &bytesReturned, NULL)) {
		LogError("Cannot send IOCTL to get snap length", stdout);
		goto Cleanup;
	}

	SecondsToTime(statistics.LoadedTime,  loadedTime);
	SecondsToTime(statistics.LoggingTime, loggingTime);

	printf(
		"Driver version . . . . . . . . . . . . . . . . . . %u.%u.%u\n"
		"Time elapsed since driver was loaded . . . . . . . %u days %u hours %u minutes %u seconds\n"
		"Time driver has had readers attached . . . . . . . %u days %u hours %u minutes %u seconds\n"
		"Total number of readers since driver was loaded  . %u\n"
		"Number of readers  . . . . . . . . . . . . . . . . %u\n"
		"Number of processes tracked by the driver  . . . . %u\n"
		"Number of connections tracked by the driver  . . . %u\n"
		"Ring buffer size . . . . . . . . . . . . . . . . . %u\n"
		"Maximum snap length  . . . . . . . . . . . . . . . %u\n"
		"This reader's ID . . . . . . . . . . . . . . . . . %u\n"
		"This reader's ring buffer size . . . . . . . . . . %u\n"
		"This reader's snap length  . . . . . . . . . . . . %u\n"
		"Total number of packets captured . . . . . . . . . %I64u\n"
		"Total number of packet bytes captured  . . . . . . %I64u\n"
		"Total number of process start events . . . . . . . %u\n"
		"Total number of process end events . . . . . . . . %u\n"
		"Total number of connection open events . . . . . . %u\n"
		"Total number of connection close events  . . . . . %u\n",
		statistics.VersionMajor, statistics.VersionMinor, statistics.VersionMicro,
		loadedTime.Days,  loadedTime.Hours,  loadedTime.Minutes,  loadedTime.Seconds,
		loggingTime.Days, loggingTime.Hours, loggingTime.Minutes, loggingTime.Seconds,
		statistics.TotalReaders,
		statistics.NumReaders,
		statistics.NumProcesses,
		statistics.NumConnections,
		statistics.RingBufferSize,
		statistics.MaxSnapLength,
		statistics.ReaderId,
		statistics.ReaderBufferSize,
		statistics.ReaderSnapLength,
		statistics.CapturedPackets,
		statistics.CapturedPacketBytes,
		statistics.ProcessStartEvents,
		statistics.ProcessEndEvents,
		statistics.ConnectionOpenEvents,
		statistics.ConnectionCloseEvents);
	rc = true;

Cleanup:
	if (driver != INVALID_HANDLE_VALUE) {
		CloseHandle(driver);
	}
	return rc;
}
