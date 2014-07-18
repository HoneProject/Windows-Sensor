//----------------------------------------------------------------------------
// Hone user-mode utility read operations
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
#include "read.h"
#include "../ioctls.h"

//--------------------------------------------------------------------------
// Defines
//--------------------------------------------------------------------------

#ifdef _X86_
#define IOCTL_HONE_SET_DATA_EVENT  IOCTL_HONE_SET_DATA_EVENT_32
#else
#define IOCTL_HONE_SET_DATA_EVENT  IOCTL_HONE_SET_DATA_EVENT_64
#endif

//--------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------

static LONG   gCleanup       = 0;
static HANDLE gDataEvent     = NULL;
static LONG   gRestart       = 0;
static time_t gLastCtrlBreak = 0;
static bool   gVerbose       = false;

//--------------------------------------------------------------------------
BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
	if (ctrlType == CTRL_BREAK_EVENT) {
		// The system can call this function more than once for a single
		// CTRL-BREAK keystroke.  Since the driver may rotate the log before the
		// second call to this function, we need to filter out calls after the
		// first one.  A simple way to do this is to make sure that more than
		// one second has elapsed between calls.
		const time_t now = time(NULL);
		if (now > gLastCtrlBreak + 2) {
			gLastCtrlBreak = now;
			if (!InterlockedCompareExchange(&gRestart, 1, 0)) {
				if (gVerbose) {
					fputs("Rotating log\n", stdout);
				}
				if (gDataEvent) {
					SetEvent(gDataEvent);
				}
			}
		}
	} else if (!InterlockedCompareExchange(&gCleanup, 1, 0)) {
		if (gVerbose) {
			fputs("Cleaning up\n", stdout);
		}
		if (gDataEvent) {
			SetEvent(gDataEvent);
		}
	}
	return TRUE;
}

//--------------------------------------------------------------------------
HANDLE OpenPcapNgFile(const char *logDir, char *filename, const size_t len)
{
	char        timeStr[16];
	__time64_t  timestamp;
	struct tm   localtime;
	char        hostname[MAX_COMPUTERNAME_LENGTH+1];
	DWORD       hostnameLen = sizeof(hostname);
	HANDLE      file        = NULL;

	if (!GetComputerName(hostname, &hostnameLen)) {
		LogError("Cannot get hostname");
		return INVALID_HANDLE_VALUE;
	}

	// Get time
	_time64(&timestamp);
	_localtime64_s(&localtime, &timestamp);
	strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &localtime);

	// Format log file name
	_snprintf_s(filename, len, len, "%s\\%s_%s.pcapng",
			logDir, hostname, timeStr);

	// Open file
	file = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
	if (file == INVALID_HANDLE_VALUE) {
		LogError("Cannot open log file %s", filename);
		return INVALID_HANDLE_VALUE;
	}

	printf("Writing events to %s\n", filename);
	return file;
}

//--------------------------------------------------------------------------
bool ReadDriver(const bool verbose, const char *logDir, UINT32 snapLen)
{
	enum State {
		STATE_NORMAL,       // Normal operation
		STATE_ROTATING,     // Rotating the log
		STATE_CLEANING_UP,  // Ensuring last record is fully read before exiting
		STATE_DONE,         // Done running
	};

	char                *buffer     = NULL;
	static const UINT32  bufferSize = 75000;
	DWORD                bytesRead;
	DWORD                bytesReturned;
	DWORD                bytesWritten;
	HANDLE               driver     = INVALID_HANDLE_VALUE;
	HANDLE               log        = INVALID_HANDLE_VALUE;
	char                 logFile[MAX_PATH];
	bool                 rc         = false;
	enum State           state      = STATE_NORMAL;
	UINT32               snapLenSet;

	gVerbose = verbose;

	if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
		LogError("Cannot set console control handler");
		goto Cleanup;
	}
	if (verbose) {
		fputs("Press CTRL-C to exit and CTRL-BREAK to rotate log\n", stdout);
	}

	gDataEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (gDataEvent == NULL) {
		LogError("Cannot create data event");
		goto Cleanup;
	}
	if (verbose) {
		printf("Data event handle is %u\n", gDataEvent);
	}

	driver = OpenDriver(verbose);
	if (driver == INVALID_HANDLE_VALUE) {
		goto Cleanup;
	}

	if (!DeviceIoControl(driver, IOCTL_HONE_SET_DATA_EVENT, &gDataEvent,
			sizeof(HANDLE), NULL, 0, &bytesReturned, NULL)) {
		LogError("Cannot send IOCTL to set data event");
		goto Cleanup;
	}

	if (!DeviceIoControl(driver, IOCTL_HONE_SET_SNAP_LENGTH, &snapLen,
			sizeof(UINT32), NULL, 0, &bytesReturned, NULL)) {
		LogError("Cannot send IOCTL to set snap length");
		goto Cleanup;
	}

	if (!DeviceIoControl(driver, IOCTL_HONE_GET_SNAP_LENGTH, NULL, 0,
			&snapLenSet, sizeof(UINT32), &bytesReturned, NULL)) {
		LogError("Cannot send IOCTL to get snap length");
		goto Cleanup;
	}
	if (verbose) {
		if (snapLenSet) {
			printf("Snap length set to %u\n", snapLenSet);
		} else {
			printf("Snap length set to 0 (unlimited)\n");
		}
	}

	log = OpenPcapNgFile(logDir, logFile, sizeof(logFile));
	if (log == INVALID_HANDLE_VALUE) {
		goto Cleanup;
	}

	buffer = reinterpret_cast<char*>(malloc(bufferSize));
	if (!buffer) {
		printf("Cannot allocate %d bytes for read buffer\n", bufferSize);
		goto Cleanup;
	}

	while (state != STATE_DONE) {
		const LONG restart = InterlockedCompareExchange(&gRestart, 0, 1);
		const LONG cleanup = InterlockedCompareExchange(&gCleanup, 0, 1);
		if (restart || cleanup) {
			if (!DeviceIoControl(driver, IOCTL_HONE_MARK_RESTART, NULL, 0, NULL, 0,
					&bytesReturned, NULL)) {
				LogError("Cannot send IOCTL to restart log");
				goto Cleanup;
			}

			if (cleanup) {
				state = STATE_CLEANING_UP;
			} else if (state == STATE_NORMAL) {
				state = STATE_ROTATING;
			}
		}

		if (!ReadFile(driver, buffer, bufferSize, &bytesRead, NULL)) {
			LogError("Cannot read %d bytes from driver", bufferSize);
			goto Cleanup;
		}
		if (bytesRead) {
			if (verbose) {
				printf("Read %d bytes\n", bytesRead);
			}

			if (!WriteFile(log, buffer, bytesRead, &bytesWritten, NULL)) {
				LogError("Cannot write %d bytes to %s", bytesRead, logFile);
				goto Cleanup;
			}
			if (bytesRead != bytesWritten) {
				printf("Only wrote %d of %d bytes to %s\n", bytesWritten, bytesRead,
						logFile);
				goto Cleanup;
			}
		} else {
			// No data to read
			switch (state) {
			case STATE_NORMAL:
				if (WaitForSingleObject(gDataEvent, INFINITE) == WAIT_FAILED) {
					LogError("Cannot wait for data event");
					goto Cleanup;
				}
				ResetEvent(gDataEvent);
				break;
			case STATE_ROTATING:
				CloseHandle(log);
				log = OpenPcapNgFile(logDir, logFile, sizeof(logFile));
				if (!log) {
					goto Cleanup;
				}
				state = STATE_NORMAL;
				break;
			default:
				state = STATE_DONE;
				break;
			}
		}
	}

	rc = true;

Cleanup:
	if (gDataEvent != NULL) {
		CloseHandle(gDataEvent);
	}
	if (driver != INVALID_HANDLE_VALUE) {
		CloseHandle(driver);
	}
	if (log != INVALID_HANDLE_VALUE) {
		CloseHandle(log);
	}
	if (buffer != NULL) {
		free(buffer);
	}
	return rc;
}
