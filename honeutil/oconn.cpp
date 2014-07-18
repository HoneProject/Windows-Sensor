//----------------------------------------------------------------------------
// Hone user-mode utility open connection operations
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

#define _WIN32_WINNT 0x0601

#include <WinSock2.h>
#include <WinIoCtl.h>
#include <ws2ipdef.h>
#include <IPHlpApi.h>

#include <conio.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "oconn.h"
#include "../ioctls.h"

//-----------------------------------------------------------------------------
void* AllocateTable(const ULONG af, const ULONG proto)
{
	const char *afStr      = (af    == AF_INET    ) ? "IPv4" : "IPv6";
	const char *protoStr   = (proto == IPPROTO_TCP) ? "TCP"  : "UDP";
	void       *table      = NULL;
	DWORD       tableSize  = 0;

	// This code has a race condition, since more connections may appear between
	// getting the table size, allocating memory for the table, and getting the
	// table.  To avoid this, we keep retrying in a loop until we succeed.  It
	// should normally take only two iterations of the loop.
	while (!table) {
		DWORD status;

		if (tableSize) {
			table = malloc(tableSize);
			if (!table) {
				printf("Cannot allocate %d bytes for %s %s table\n", tableSize,
						afStr, protoStr);
				return NULL;
			}
		}

		if (proto == IPPROTO_TCP) {
			status = GetExtendedTcpTable(table, &tableSize, TRUE, af,
					TCP_TABLE_OWNER_MODULE_ALL, 0);
		} else {
			status = GetExtendedUdpTable(table, &tableSize, TRUE, af,
					UDP_TABLE_OWNER_MODULE, 0);
		}
		if (status != NO_ERROR) {
			free(table);
			table = NULL;
			if (status != ERROR_INSUFFICIENT_BUFFER) {
				LogError(status, "Cannot get %s %s table", afStr, protoStr);
				break;
			}
		}
	}
	return table;
}

//-----------------------------------------------------------------------------
UINT16 NetToHost(const DWORD val)
{
	return ((val >> 8) & 0x00FF) | ((val << 8) & 0xFF00);
}

//-----------------------------------------------------------------------------
CONNECTIONS* ParseRecords(
	MIB_TCPTABLE_OWNER_MODULE  *tableTcpV4,
	MIB_TCP6TABLE_OWNER_MODULE *tableTcpV6,
	MIB_UDPTABLE_OWNER_MODULE  *tableUdpV4,
	MIB_UDP6TABLE_OWNER_MODULE *tableUdpV6)
{
	UINT32       bytes       = 0;
	CONNECTIONS *connections = NULL;
	UINT32       index;
	UINT32       numRecords  = 0;
	UINT32       recordIndex = 0;

	if (tableTcpV4) {
		numRecords += tableTcpV4->dwNumEntries;
	}
	if (tableTcpV6) {
		numRecords += tableTcpV6->dwNumEntries;
	}
	if (tableUdpV4) {
		numRecords += tableUdpV4->dwNumEntries;
	}
	if (tableUdpV6) {
		numRecords += tableUdpV6->dwNumEntries;
	}

	bytes = offsetof(CONNECTIONS, Records) + (numRecords * sizeof(CONNECTION_RECORD));
	connections = reinterpret_cast<CONNECTIONS*>(malloc(bytes));
	if (!connections) {
		printf("Cannot allocate %d bytes of data for %d connection records\n",
				bytes, numRecords);
		return NULL;
	}
	connections->NumRecords = numRecords;

	if (tableTcpV4) {
		for (index = 0; index < tableTcpV4->dwNumEntries; index++) {
			connections->Records[recordIndex].AddressFamily = AF_INET;
			connections->Records[recordIndex].Protocol      = IPPROTO_TCP;
			connections->Records[recordIndex].Port          = NetToHost(tableTcpV4->table[index].dwLocalPort);
			connections->Records[recordIndex].ProcessId     = tableTcpV4->table[index].dwOwningPid;
			connections->Records[recordIndex].Timestamp     = tableTcpV4->table[index].liCreateTimestamp;
			recordIndex++;
		}
	}

	if (tableTcpV6) {
		for (index = 0; index < tableTcpV6->dwNumEntries; index++) {
			connections->Records[recordIndex].AddressFamily = AF_INET6;
			connections->Records[recordIndex].Protocol      = IPPROTO_TCP;
			connections->Records[recordIndex].Port          = NetToHost(tableTcpV6->table[index].dwLocalPort);
			connections->Records[recordIndex].ProcessId     = tableTcpV6->table[index].dwOwningPid;
			connections->Records[recordIndex].Timestamp     = tableTcpV6->table[index].liCreateTimestamp;
			recordIndex++;
		}
	}

	if (tableUdpV4) {
		for (index = 0; index < tableUdpV4->dwNumEntries; index++) {
			connections->Records[recordIndex].AddressFamily = AF_INET;
			connections->Records[recordIndex].Protocol      = IPPROTO_UDP;
			connections->Records[recordIndex].Port          = NetToHost(tableUdpV4->table[index].dwLocalPort);
			connections->Records[recordIndex].ProcessId     = tableUdpV4->table[index].dwOwningPid;
			connections->Records[recordIndex].Timestamp     = tableUdpV4->table[index].liCreateTimestamp;
			recordIndex++;
		}
	}

	if (tableUdpV6) {
		for (index = 0; index < tableUdpV6->dwNumEntries; index++) {
			connections->Records[recordIndex].AddressFamily = AF_INET6;
			connections->Records[recordIndex].Protocol      = IPPROTO_UDP;
			connections->Records[recordIndex].Port          = NetToHost(tableUdpV6->table[index].dwLocalPort);
			connections->Records[recordIndex].ProcessId     = tableUdpV6->table[index].dwOwningPid;
			connections->Records[recordIndex].Timestamp     = tableUdpV6->table[index].liCreateTimestamp;
			recordIndex++;
		}
	}

	return connections;
}

//-----------------------------------------------------------------------------
void PrintRecords(CONNECTIONS *connections)
{
	UINT32 index;

	printf(
			"Have %d open connection records\n"
			"\n"
			"Index Family Proto PID    Port  Timestamp\n"
			"----- ------ ----- ------ ----- -----------------------\n",
			connections->NumRecords);

	for (index = 0; index < connections->NumRecords; index++) {
		FILETIME   ft;
		SYSTEMTIME st;
		const char *afStr    = (connections->Records[index].AddressFamily == AF_INET    ) ? "IPv4  " : "IPv6  ";
		const char *protoStr = (connections->Records[index].Protocol      == IPPROTO_TCP) ? "TCP  "  : "UDP  ";

		ft.dwLowDateTime  = connections->Records[index].Timestamp.LowPart;
		ft.dwHighDateTime = connections->Records[index].Timestamp.HighPart;
		FileTimeToSystemTime(&ft, &st);

		printf("%5d %s %s %6d %5d %04d-%02d-%02d %02d:%02d:%02d.%03d\n",
				index, afStr, protoStr, connections->Records[index].ProcessId,
				connections->Records[index].Port, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}
}

//-----------------------------------------------------------------------------
bool SendRecords(CONNECTIONS *connections)
{
	DWORD  bytesReturned;
	DWORD  bytesToSend;
	HANDLE driver = INVALID_HANDLE_VALUE;

	driver = OpenDriver(false);
	if (driver == INVALID_HANDLE_VALUE) {
		return false;
	}

	bytesToSend = sizeof(connections->NumRecords) +
			(connections->NumRecords * sizeof(CONNECTION_RECORD));
	if (!DeviceIoControl(driver, IOCTL_HONE_SET_OPEN_CONNECTIONS, connections,
			bytesToSend, NULL, 0, &bytesReturned, NULL)) {
		LogError("Cannot send IOCTL to set open connections");
		CloseHandle(driver);
		return false;
	}

	CloseHandle(driver);
	return true;
}

//-----------------------------------------------------------------------------
bool SendOptionConnections(const bool verbose)
{
	MIB_TCPTABLE_OWNER_MODULE  *tableTcpV4  = NULL;
	MIB_TCP6TABLE_OWNER_MODULE *tableTcpV6  = NULL;
	MIB_UDPTABLE_OWNER_MODULE  *tableUdpV4  = NULL;
	MIB_UDP6TABLE_OWNER_MODULE *tableUdpV6  = NULL;
	CONNECTIONS                *connections = NULL;
	bool                        rc          = true;

	tableTcpV4 = reinterpret_cast<MIB_TCPTABLE_OWNER_MODULE*>(AllocateTable(AF_INET, IPPROTO_TCP));
	tableTcpV6 = reinterpret_cast<MIB_TCP6TABLE_OWNER_MODULE*>(AllocateTable(AF_INET6, IPPROTO_TCP));
	tableUdpV4 = reinterpret_cast<MIB_UDPTABLE_OWNER_MODULE*>(AllocateTable(AF_INET, IPPROTO_UDP));
	tableUdpV6 = reinterpret_cast<MIB_UDP6TABLE_OWNER_MODULE*>(AllocateTable(AF_INET6, IPPROTO_UDP));

	connections = ParseRecords(tableTcpV4, tableTcpV6, tableUdpV4, tableUdpV6);
	if (connections) {
		if (verbose) {
			PrintRecords(connections);
		}
		rc = SendRecords(connections);
	}

	free(tableTcpV4);
	free(tableTcpV6);
	free(tableUdpV4);
	free(tableUdpV6);
	free(connections);
	return rc;
}
