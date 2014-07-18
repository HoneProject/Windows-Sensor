//----------------------------------------------------------------------------
// Hone user-mode utility
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
#include <conio.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filters.h"
#include "oconn.h"
#include "read.h"
#include "stats.h"

//--------------------------------------------------------------------------
// Structures and enumerations
//--------------------------------------------------------------------------

enum Operations {
	OpNone,
	OpGetStatistics,
	OpInstallFilters,
	OpRead,
	OpSendOpenConnections,
	OpUninstallFilters,
};

//--------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------

static const char *gLogDir     = ".";
static Operations  gOperation  = OpNone;
static bool        gPause      = false;
static bool        gVerbose    = false;
static UINT32      gSnapLength = 0;

//--------------------------------------------------------------------------
bool StrToUInt32(const char *str, UINT32 &val, const char *msg)
{
	UINT32  temp;
	char   *endptr;

	errno = 0;
#pragma warning(push)
#pragma warning(disable:28193) // We examine the value, so ignore this warning
	temp = strtoul(str, &endptr, 0);
#pragma warning(pop)
	if (*endptr != '\0') {
		printf("Invalid %s \"%s\": Invalid character \"%c\"\n", msg, str, *endptr);
		return false;
	} else if (errno) {
		char buffer[256];
		strerror_s(buffer, sizeof(buffer), errno);
		buffer[sizeof(buffer)-1] = '\0';
		printf("Invalid %s \"%s\": %s\n", msg, str, buffer);
		return false;
	} else if (strtol(str, NULL, 0) < 0) {
		printf("Invalid %s \"%s\": Result negative\n", msg, str);
		return false;
	}
	val = temp;
	return true;
}

//--------------------------------------------------------------------------
bool ParseArgs(const int argc, char const* const* argv)
{
	int  index;
	bool rc;
	int  errors = 0;

	if (argc < 2) {
		fputs("You must specify an operation to perform\n", stdout);
		return false;
	}

	if (strcmp(argv[1], "-h") == 0) {
		return false;
	} else if (strcmp(argv[1], "read") == 0) {
		gOperation = OpRead;
	} else if (strcmp(argv[1], "get-stats") == 0) {
		gOperation = OpGetStatistics;
	} else if (strcmp(argv[1], "send-conns") == 0) {
		gOperation = OpSendOpenConnections;
	} else if (strcmp(argv[1], "install") == 0) {
		gOperation = OpInstallFilters;
	} else if (strcmp(argv[1], "uninstall") == 0) {
		gOperation = OpUninstallFilters;
	} else {
		printf("Unknown command \"%s\"\n", argv[1]);
		return false;
	}

	for (index = 2; index < argc; index++) {
		if ((argv[index][0] != '-') ||
				(argv[index][1] == '\0') ||
				(argv[index][2] != '\0')) {
			printf("Unknown option \"%s\"\n", argv[index]);
			errors++;
			continue;
		}
		switch (argv[index][1]) {
		case 'd':
			if (index + 1 >= argc) {
				printf("You must supply a directory name with the %s option\n",
						argv[index]);
				rc = false;
			} else {
				index++;
				gLogDir = argv[index];
			}
			break;
		case 'h':
			return false;
		case 'p':
			gPause = true;
			break;
		case 's':
			if (index + 1 >= argc) {
				printf("You must supply a snap length with the %s option\n",
						argv[index]);
				rc = false;
			} else {
				index++;
				if (StrToUInt32(argv[index], gSnapLength, "snap length") == false) {
					rc = false;
				}
			}
			break;
		case 'v':
			gVerbose = true;
			break;
		default:
			printf("Unknown option \"%s\"\n", argv[index]);
			errors++;
			break;
		}
	}

	return errors ? false : true;
}

//--------------------------------------------------------------------------
void Usage(const char *progname)
{
	printf("Usage: %s command [options]\n"
			"Commands:\n"
			"  read        Read captured data from the driver\n"
			"  get-stats   Get driver statistics\n"
			"  send-conns  Send open connections to the driver\n"
			"  install     Install network filters used by the driver\n"
			"  uninstall   Uninstall network filters used by the driver\n"
			"Options:\n"
			"  -h        Help (this text)\n"
			"  -d dir    Output file directory (default: current directory)\n"
			"  -p        Pause before exiting\n"
			"  -s bytes  The snap length in bytes (default: unlimited)\n"
			"  -v        Verbose output\n",
			progname);
}

//--------------------------------------------------------------------------
int __cdecl main(int argc, char *argv[])
{
	bool rc = true;

	if (!ParseArgs(argc, argv)) {
		fputc('\n', stdout);
		Usage(argv[0]);
		rc = false;
	} else {
		switch (gOperation) {
		case OpGetStatistics:
			rc = GetStatistics(gVerbose, gSnapLength);
			break;
		case OpInstallFilters:
			rc = SetupFilters(gVerbose, true);
			break;
		case OpRead:
			rc = ReadDriver(gVerbose, gLogDir, gSnapLength);
			break;
		case OpSendOpenConnections:
			rc = SendOptionConnections(gVerbose);
			break;
		case OpUninstallFilters:
			rc = SetupFilters(gVerbose, false);
			break;
		default:
			break;
		}
	}

	if (gPause) {
		fputs("\nPress any key to continue . . .\n", stdout);
		_getch();
	}
	return rc ? 0 : 1;
}
