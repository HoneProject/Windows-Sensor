//----------------------------------------------------------------------------
// IOCTLs supported by the Hone driver
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

#ifndef IOCTLS_H
#define IOCTLS_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

// IOCTL function codes
enum IOCTL_FUNCTIONS {
	IoctlRestart,
	IoctlFilterConnection,
	IoctlSetSnapLength,
	IoctlGetSnapLength,
	IoctlSetDataEvent,
	IoctlOpenConnections,
	IoctlGetStatistics,
	IoctlFlag   = 0x800, // Start of user-defined IOCTL function range
	IoctlFlag64 = 0xC00, // Used for IOCTLs that require a 64-bit version
};

#pragma pack(push, 4) // Ensure structures are 4 byte aligned

struct CONNECTION_RECORD {
	UINT16        Port;           // Connection port
	UINT8         AddressFamily;  // Addess family for the connection (IPv4/IPv6)
	UINT8         Protocol;       // Protocol for the connection (TCP/UDP)
	UINT32        ProcessId;      // Process that owns the connection
	LARGE_INTEGER Timestamp;      // Time connection was opened
};

struct CONNECTIONS {
	UINT32                   NumRecords;  // Number of records in the array
	struct CONNECTION_RECORD Records[1];  // Array of connection records
};

struct STATISTICS {
	UINT8  VersionMajor;           // Major version number (year)
	UINT8  VersionMinor;           // Minor version number (month)
	UINT16 VersionMicro;           // Micro version number (day)
	UINT32 LoadedTime;             // Number of seconds elapsed since the driver was loaded
	UINT32 LoggingTime;            // Number of seconds that readers have been attached
	UINT32 NumReaders;             // Current number of attached readers
	UINT32 TotalReaders;           // Total number of attached readers since driver was loaded
	UINT32 ReaderId;               // Reader's ID
	UINT32 RingBufferSize;         // Current ring buffer size
	UINT32 ReaderBufferSize;       // Reader's ring buffer size
	UINT32 MaxSnapLength;          // Current maximum snap length
	UINT32 ReaderSnapLength;       // Reader's snap length
	UINT64 CapturedPackets;        // Total number of PCAP-NG packet blocks captured
	UINT64 CapturedPacketBytes;    // Total number of packet bytes captured
	LONG   ProcessStartEvents;     // Total number of process start events
	LONG   NumProcesses;           // Current number of processes
	LONG   ProcessEndEvents;       // Total number of process end events
	LONG   ConnectionOpenEvents;   // Total number of connection open events
	LONG   NumConnections;         // Current number of connections
	LONG   ConnectionCloseEvents;  // Total number of connection close events
};

#pragma pack(pop)

//----------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------

/// @brief Marks a reset request
///
/// A reset request allows a reader to rotate a log without truncating a
/// block.  Reset requests are on a per-reader basis and work as follows:
///
/// * The reader sends this IOCTL, and the driver marks the reset request.
/// * The reader continues to read from the driver until it returns 0 bytes
///   read.  If there is no current block, the driver will return 0 bytes
///   read.  Otherwise, it will finish the current block.
/// * When the reader gets 0 bytes read, it closes its log and opens a new one.
/// * The reader reads from the driver.  The driver returns PCAP-NG section
///   header and interface description blocks, followed by blocks for all
///   of the running processes and open connections.  After that, it resumes
///   normal processing.
#define IOCTL_HONE_MARK_RESTART CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlRestart, METHOD_NEITHER, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/// @brief Registers a connection ID to filter packet blocks for
///
/// * The reader passes the ID of the connection to filter in the buffer, which
///   must be at least 4 bytes in length
/// * A connection ID of 0 disables filtering
/// * Read operations will not return any packet block that has the filtered
///   connection ID
/// * Filtered IDs are on a per-reader basis, so different readers can filter
///   blocks for different IDs
#define IOCTL_HONE_FILTER_CONNECTION CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlFilterConnection, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/// @brief Sets the amount of data captured from packets (the snap length)
///
/// * The reader passes the snap length in the buffer, which must be at least 4
///   bytes in length
/// * A snap length of 0 sets snap length to unlimited (4GB)
/// * The maximum snap length is 0xFFFFFFFF (4GB)
/// * Setting the snap length does not alter packets that are already captured
///   and queued in the buffer
#define IOCTL_HONE_SET_SNAP_LENGTH CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlSetSnapLength, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/// @brief Gets the amount of data captured from packets (the snap length)
#define IOCTL_HONE_GET_SNAP_LENGTH CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlGetSnapLength, METHOD_BUFFERED, FILE_READ_ACCESS)

/// @brief Sets the handle to the event to call when data is available
///
/// * The reader passes the event in the buffer, which must be at large enough
///   to hold a pointer
/// * An event of 0 disables data event signaling
#define IOCTL_HONE_SET_DATA_EVENT_32 CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlSetDataEvent, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_HONE_SET_DATA_EVENT_64 CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag64 | \
	IoctlSetDataEvent, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/// @brief Passes a list of open connections to the driver
///
/// * The reader passes the list in the buffer, which contains a populated
///   CONNECTION_LIST structure
/// * The buffer must be large enough to hold the list
/// * Passing a new list of open connections will overwrite the previously
///   stored list
/// * Passing a list with no connection records clears the stored list of
///   connections
#define IOCTL_HONE_SET_OPEN_CONNECTIONS CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlOpenConnections, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/// @brief Gets driver statistics, such as version, uptime, packets captured, etc.
///
/// * The reader passes a buffer, which must be large enough to hold a
///   statistics structure
/// * The driver returns the statistics in the buffer
#define IOCTL_HONE_GET_STATISTICS CTL_CODE(FILE_DEVICE_UNKNOWN, IoctlFlag | \
	IoctlGetStatistics, METHOD_BUFFERED, FILE_READ_ACCESS)

#ifdef __cplusplus
};
#endif

#endif // IOCTLS_H
