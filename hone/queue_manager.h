//----------------------------------------------------------------------------
// Manages queues that hold data collected by the Hone driver
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

#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "common.h"
#include "llrb_clear.h"
#include "ring_buffer.h"
#include "../ioctls.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------

#ifndef PCAP_NG_PADDING
#define PCAP_NG_PADDING(x) ((x) + ((4-(x)) & 0x03))
#endif

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

// Supported PCAP-NG block types
enum BLOCK_TYPES {
	ConnectionBlock           = 0x00000102,
	InterfaceDescriptionBlock = 0x00000001,
	PacketBlock               = 0x00000006,
	ProcessBlock              = 0x00000101,
	SectionHeaderBlock        = 0x0A0D0D0A,
};

#pragma pack(push, 4) // PCAP-NG structures are 32-bit aligned

// PCAP-NG block option header
struct PCAP_NG_OPTION_HEADER {
	UINT16 OptionCode;
	UINT16 OptionLength;
};

// PCAP-NG connection block format:
//
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +---------------------------------------------------------------+
//  0 |                    Block Type = 0x00000102                    |
//    +---------------------------------------------------------------+
//  4 |                      Block Total Length                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |                        Connection ID                          |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |                          Process ID                           |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                        Timestamp (High)                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 20 |                        Timestamp (Low)                        |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 24 /                                                               /
//    /                      Options (variable)                       /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      Block Total Length                       |
//    +---------------------------------------------------------------+
//
struct PCAP_NG_CONNECTION_HEADER {
	UINT32 BlockType;
	UINT32 BlockLength;
	UINT32 ConnectionId;
	UINT32 ProcessId;
	UINT32 TimestampHigh;
	UINT32 TimestampLow;
	// Options and block length
};

// PCAP-NG interface description block format:
//
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +---------------------------------------------------------------+
//  0 |                    Block Type = 0x00000001                    |
//    +---------------------------------------------------------------+
//  4 |                      Block Total Length                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |           LinkType            |           Reserved            |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |                            SnapLen                            |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 /                                                               /
//    /                      Options (variable)                       /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      Block Total Length                       |
//    +---------------------------------------------------------------+
//
struct PCAP_NG_INTERFACE_DESCRIPTION {
	UINT32                       BlockType;
	UINT32                       BlockLength;
	UINT16                       LinkType;
	UINT16                       Reserved;
	UINT32                       SnapLength;
	struct PCAP_NG_OPTION_HEADER IfDescHeader;
	char                         IfDesc[28];
	struct PCAP_NG_OPTION_HEADER OptionEnd;
	UINT32                       BlockLengthFooter;
};

// PCAP-NG enhanced packet block format:
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +---------------------------------------------------------------+
//  0 |                    Block Type = 0x00000006                    |
//    +---------------------------------------------------------------+
//  4 |                      Block Total Length                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |                         Interface ID                          |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |                        Timestamp (High)                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                        Timestamp (Low)                        |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 20 |                         Captured Len                          |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 24 |                          Packet Len                           |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 28 /                                                               /
//    /                          Packet Data                          /
//    /           ( variable length, aligned to 32 bits )             /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    /                                                               /
//    /                      Options (variable)                       /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      Block Total Length                       |
//    +---------------------------------------------------------------+
//
struct PCAP_NG_PACKET_HEADER {
	UINT32 BlockType;
	UINT32 BlockLength;
	UINT32 InterfaceId;
	UINT32 TimestampHigh;
	UINT32 TimestampLow;
	UINT32 CapturedLength;
	UINT32 PacketLength;
	// Block data, options, and block length
};

struct PCAP_NG_PACKET_FOOTER {
	struct PCAP_NG_OPTION_HEADER ConnectionIdHeader;
	UINT32                       ConnectionId;
	struct PCAP_NG_OPTION_HEADER ProcessIdHeader;
	UINT32                       ProcessId;
	struct PCAP_NG_OPTION_HEADER FlagsHeader;
	UINT32                       Flags;
	struct PCAP_NG_OPTION_HEADER OptionEnd;
	UINT32                       BlockLength;
};

// PCAP-NG process event block format:
//
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +---------------------------------------------------------------+
//  0 |                    Block Type = 0x00000101                    |
//    +---------------------------------------------------------------+
//  4 |                      Block Total Length                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |                          Process ID                           |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |                        Timestamp (High)                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                        Timestamp (Low)                        |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 20 /                                                               /
//    /                      Options (variable)                       /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      Block Total Length                       |
//    +---------------------------------------------------------------+
//
struct PCAP_NG_PROCESS_HEADER {
	UINT32                       BlockType;
	UINT32                       BlockLength;
	UINT32                       ProcessId;
	UINT32                       TimestampHigh;
	UINT32                       TimestampLow;
	struct PCAP_NG_OPTION_HEADER ParentPidHeader;
	UINT32                       ParentPid;
	// Options and block length
};

// PCAP-NG section header block format:
//
//   0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +---------------------------------------------------------------+
//  0 |                   Block Type = 0x0A0D0D0A                     |
//    +---------------------------------------------------------------+
//  4 |                      Block Total Length                       |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  8 |                      Byte-Order Magic                         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 |          Major Version        |         Minor Version         |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 |                                                               |
//    |                          Section Length                       |
//    |                                                               |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 24 /                                                               /
//    /                      Options (variable)                       /
//    /                                                               /
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      Block Total Length                       |
//    +---------------------------------------------------------------+
//
struct PCAP_NG_SECTION_HEADER {
	UINT32 BlockType;
	UINT32 BlockLength;
	UINT32 ByteOrder;
	UINT16 MajorVersion;
	UINT16 MinorVersion;
	UINT64 SectionLength;
	// Options and block length
};

#pragma pack(pop)

// An LLRB tree node that holds a PCAP-NG block
// The PCAP-NG data is in the Data member if the block is large enough to
// contain all of it.  Otherwise, it is in the buffer pointed to by Buffer.
typedef bool _Bool;
struct BLOCK_NODE {
	LLRB_ENTRY(BLOCK_NODE) TreeEntry;    // LLRB tree entry
	LIST_ENTRY             ListEntry;    // Doubly-linked list of blocks
	LONG                   RefCount;     // Block reference count
	UINT32                 BlockType;    // Block type to aid in debugging
	UINT32                 BlockLength;  // Block data length in bytes
	UINT32                 SortId;       // Process or connection ID for sorting
	UINT32                 ConnectionId; // Connection ID (0 if none)
	UINT32                 ProcessId;    // Process ID (0xFFFFFFFF if none, since 0 is a valid PID)
	LARGE_INTEGER          Timestamp;    // Block timestamp in milliseconds since 1970-01-01
	char                  *Buffer;       // Buffer to use if this block isn't large enough, NULL otherwise
	char                   Data[512];    // Block data
};

// Information about a registered reader
struct READER_INFO {
	LIST_ENTRY   ListEntry;       // Doubly-linked list of readers
	RING_BUFFER  BlocksBuffer;    // Ring buffer that holds PCAP-NG blocks for normal processing
	RING_BUFFER  InitialBuffer;   // Ring buffer that holds initial PCAP-NG blocks when resetting
	UINT32       SnapLength;      // Number of bytes to capture (0 if none, 0xFFFFFFFF if unlimited)
	UINT32       Id;              // Unique ID for this reader
	UINT32       RingBufferSize;  // Size of blocks ring buffer
	KEVENT      *DataEvent;       // Event to signal when data is available (NULL if none)
};

enum PACKET_DIRECTION {
	Inbound  = 1,
	Outbound = 2,
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Deinitializes the queues
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS DeinitializeQueueManager(void);

//----------------------------------------------------------------------------
/// @brief Initializes the queues
///
/// @param device  WDM device object for this driver
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS InitializeQueueManager(__in DEVICE_OBJECT *device);

//----------------------------------------------------------------------------
/// @brief Allocates a buffer to hold a packet block
///
/// @param dataLength  Bytes of packet data to reserve in the block
/// @param dataBuffer  Stores pointer to data buffer in the block
///
/// @returns New packet block buffer if successful; NULL otherwise
__checkReturn
BLOCK_NODE *QmAllocatePacketBlock(
	__in const UINT32   dataLength,
	__in char         **dataBuffer);

//----------------------------------------------------------------------------
/// @brief Decrements block reference count and frees memory when count is zero
///
/// @param block  Block to clean up
///
/// @returns True if the block memory was freed; false otherwise
bool QmCleanupBlock(__in BLOCK_NODE *blockNode);

//----------------------------------------------------------------------------
/// @brief Dequeues the next available block
///
/// @param reader  Reader to get block for
///
/// @returns Block if successful; NULL otherwise
__checkReturn
BLOCK_NODE *QmDequeueBlock(__in READER_INFO *reader);

//----------------------------------------------------------------------------
/// @brief Removes the reader's buffer and associated information
///
/// @param reader  Reader to remove
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmDeregisterReader(__in READER_INFO *reader);

//----------------------------------------------------------------------------
/// @brief Enqueues a connection block
///
/// @param opened        Connection opened if true and closed if false
/// @param connectionId  ID of the connection
/// @param processId     ID of the process that owns the connection
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmEnqueueConnectionBlock(
	__in const bool   opened,
	__in const UINT32 connectionId,
	__in const UINT32 processId);

//----------------------------------------------------------------------------
/// @brief Enqueues a packet block
///
/// Use QueuesAllocatePacketBuffer() to allocate a packet block buffer.
///
/// @param blockNode       Previously allocated packet block with packet data
/// @param direction       Packet direction (inbound or outbound)
/// @param capturedLength  Number of bytes captured
/// @param packetLength    Total length of packet in bytes
/// @param connectionId    ID of the connection handling the packet
/// @param addressFamily   Address family for the packet (IPv4/IPv6)
/// @param protocol        Protocol for the packet (TCP/UDP)
/// @param port            Local port associated with the packet
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmEnqueuePacketBlock(
	__in BLOCK_NODE             *blockNode,
	__in const PACKET_DIRECTION  direction,
	__in const UINT32            capturedLength,
	__in const UINT32            packetLength,
	__in const UINT32            connectionId,
	__in const UINT16            addressFamily,
	__in const UINT8             protocol,
	__in const UINT16            port);

//----------------------------------------------------------------------------
/// @brief Enqueues a process block
///
/// @param started    Process started if true and stopped if false
/// @param pid        ID of the process
/// @param parentPid  ID of the process's parent
/// @param path       Process path string (NULL if none)
/// @param args       Process argument string (NULL if none)
/// @param sid        Process owner security ID string (NULL if none)
/// @param timestamp  Process start kernel timestamp (NULL for current time)
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmEnqueueProcessBlock(
	__in const bool            started,
	__in const UINT32          pid,
	__in const UINT32          parentPid,
	__in UNICODE_STRING       *path      = NULL,
	__in UNICODE_STRING       *args      = NULL,
	__in UNICODE_STRING       *sid       = NULL,
	__in const LARGE_INTEGER  *timestamp = NULL);

//----------------------------------------------------------------------------
/// @brief Gets all open process and connection blocks
///
/// When the reader is initializing and hasn't been added to the reader list,
/// you can set useBlocksBuffer to true to use the already allocated blocks
/// buffer.  Afterwards, you need to set useBlocksBuffer to false to allocate
/// a separate buffer to store the initial blocks, since the driver may be
/// enqueuing other blocks on the blocks buffer at the same time.
///
/// @param reader           Reader to get blocks for
/// @param useBlocksBuffer  Use blocks buffer if true, initial buffer if false
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmGetInitialBlocks(
	__in READER_INFO *reader,
	__in const bool   useBlocksBuffer);

//----------------------------------------------------------------------------
/// @brief Gets maximum snap length for all registered readers
///
/// @returns Maximum snap length
UINT32 QmGetMaxSnapLen(void);

//----------------------------------------------------------------------------
/// @brief Gets the number of registered readers
///
/// @returns Number of registered readers
UINT32 QmGetNumReaders(void);

//----------------------------------------------------------------------------
/// @brief Gets driver and reader statistics
///
/// @param statistics  Structure to hold statistics
/// @param reader      Reader to get reader statistics for
void QmGetStatistics(__in STATISTICS *statistics, __in READER_INFO *reader);

//----------------------------------------------------------------------------
/// @brief Registers a reader to receive blocks
///
/// Allocates memory for the reader's ring buffers and copies all initial
/// records into the reader's initial records ring buffer
///
/// @param reader  Reader to register
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmRegisterReader(__in READER_INFO *reader);

//----------------------------------------------------------------------------
/// @brief Provides a list of currently open connections
///
/// There is no kernel-mode API to get a list of currently open connections,
/// so the driver has to rely on a user-mode program to supply this list.
/// This allows the driver to correlate packets to processes for connections
/// that were already open when the driver was loaded.  This isn't an issue
/// when the driver loads a boot time, since it loads before the network is
/// available.
///
/// @param connections  List of currently open connections
void QmSetOpenConnections(__in CONNECTIONS *connections);

//----------------------------------------------------------------------------
/// @brief Sets the specified reader's data notify event handle
///
/// @param reader  Reader to set data notify event for
/// @param event   Data notify event handle (0 to disable)
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmSetReaderDataEvent(
	__in READER_INFO  *reader,
	__in const HANDLE  userEvent);

//----------------------------------------------------------------------------
/// @brief Sets the specified reader's snap length
///
/// @param reader      Reader to set snap length for
/// @param snapLength  New snap length (0xFFFFFFFF for unlimited)
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS QmSetReaderSnapLength(
	__in READER_INFO  *reader,
	__in const UINT32  snapLength);

#ifdef __cplusplus
};
#endif

#endif  // QUEUE_MANAGER_H
