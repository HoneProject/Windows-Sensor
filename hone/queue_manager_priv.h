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

#ifndef QUEUE_MANAGER_PRIV_H
#define QUEUE_MANAGER_PRIV_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "queue_manager.h"

#include <ntstrsafe.h>
#include <ws2def.h>

#include "hone_info.h"
#include "debug_print.h"
#include "system_id.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

// An LLRB tree node that holds information for an open connection
struct OCONN_NODE {
	LLRB_ENTRY(OCONN_NODE) TreeEntry;   // LLRB tree entry
	UINT16                 Port;        // Connection port
	UINT32                 ProcessId;   // Process that owns the connection
	LARGE_INTEGER          Timestamp;   // Time connection was opened
};

// LLRB tree structures
typedef LLRB_HEAD(BlockTree, BLOCK_NODE) BLOCK_TREE_HEAD;
typedef LLRB_HEAD(OconnTree, OCONN_NODE) OCONN_TREE_HEAD;

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Allocates memory for a block node
///
/// Clears the memory for the block node header and sets the block's reference
/// count to 1, but does not clear the memory for the block itself
///
/// @brief dataLength  Length of the block data in bytes
/// @brief poolTag     Tag to use if allocating additional memory for data buffer
__checkReturn
BLOCK_NODE* AllocateBlockNode(
	__in const UINT32 dataLength,
	__in const UINT32 poolTag);

//----------------------------------------------------------------------------
/// @brief Calculates the maximum snap length of all registered readers
void CalculateMaxSnapLength(void);

//----------------------------------------------------------------------------
/// @brief Frees a node in an open connection tree
///
/// @param oconnNode  Node to clean up
void CleanupOconnNode(__in OCONN_NODE *oconnNode);

//----------------------------------------------------------------------------
/// @brief Frees resources held by a reader
///
/// @param reader  Reader to clean up
void CleanupReader(__in READER_INFO *reader);

//----------------------------------------------------------------------------
/// @brief Deletes all blocks from a ring buffer and resets its state
///
/// @param buffer  Ring buffer to clean up
void CleanupRingBuffer(__in RING_BUFFER *buffer);

//----------------------------------------------------------------------------
/// @brief Compare two block nodes for sorting the LLRB tree
///
/// @param first   First block node to compare
/// @param second  Second block node to compare
///
/// @returns <0 if first node's block ID is less than second;
///           0 if nodes' block IDs are equal
///          >0 if first node's block ID is greater than second
int CompareBlockNodes(BLOCK_NODE *first, BLOCK_NODE *second);

//----------------------------------------------------------------------------
/// @brief Compare two open connection nodes for sorting the LLRB tree
///
/// @param first   First open connection node to compare
/// @param second  Second open connection node to compare
///
/// @returns <0 if first node's port is less than second;
///           0 if nodes' ports are equal
///          >0 if first node's port is greater than second
int CompareOconnNodes(OCONN_NODE *first, OCONN_NODE *second);

//----------------------------------------------------------------------------
/// @brief Converts a command line string to a null-separated argv list in place
///
/// http://msdn.microsoft.com/en-us/library/17w5ykft.aspx documents the
/// algorithm used by the C runtime and the CommandLineToArgvW function to parse
/// the command line.  However, there are additional rules for handling double
/// quotes and parsing the first argument that are not documented in MSDN.  For
/// a more detailed discussion on the command line parsing algorithm, see
/// http://www.daviddeley.com/autohotkey/parameters/parameters.htm.
///
/// @param buffer  Buffer containing the string to fix up
/// @param length  Length of string in bytes, without terminating null
///
/// @returns New length of the command line string in bytes
UINT16 ConvertCommandLineToArgv(__in char *buffer, __in const UINT16 length);

//----------------------------------------------------------------------------
/// @brief Converts windows kernel timestamp to PCAP-NG timestamp
///
/// @param in   Timestamp in windows kernel format to convert
/// @param out  Timestamp converted to PCAP-NG format
void ConvertKeTime(__in const LARGE_INTEGER *in, __out LARGE_INTEGER *out);

//----------------------------------------------------------------------------
/// @brief Called when DLL is initialized
///
/// @param registryPath  Path to the driver's registry keys
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
NTSTATUS DllInitialize(__in PUNICODE_STRING registryPath);

//----------------------------------------------------------------------------
/// @brief Called when DLL is unloaded
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
NTSTATUS DllUnload(void);

//----------------------------------------------------------------------------
/// @brief Main driver entry point
///
/// @param driverObject  This driver's object
/// @param registryPath  Path to the driver's registry keys
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
DRIVER_INITIALIZE DriverEntry;

//----------------------------------------------------------------------------
/// @brief Enqueues a block on all reader ring buffers
///
/// @param blockNode  Block to enqueue
__checkReturn
void EnqueueBlock(__in BLOCK_NODE *blockNode);

//----------------------------------------------------------------------------
/// @brief Allocates and populates PCAP-NG connection block
///
/// The block's reference count is already set to 1
///
/// @param opened        Connection opened if true and closed if false
/// @param connectionId  ID of the connection
/// @param processId     ID of the process that owns the connection
/// @param timestamp     Connection event kernel timestamp (NULL or zero for current time)
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
BLOCK_NODE* GetConnectionBlock(
	__in const bool            opened,
	__in const UINT32          connectionId,
	__in const UINT32          processId,
	__in const LARGE_INTEGER  *timestamp);

//----------------------------------------------------------------------------
/// @brief Allocates and populates PCAP-NG interface description block
///
/// The block's reference count is already set to 1
///
/// @returns The block if successful; NULL otherwise
__checkReturn
BLOCK_NODE* GetInterfaceDescriptionBlock(void);

//----------------------------------------------------------------------------
/// @brief Allocates and populates PCAP-NG process block
///
/// The block's reference count is already set to 1
///
/// @param started    Process started if true and stopped if false
/// @param pid        ID of the process
/// @param parentPid  ID of the process's parent
/// @param path       Process path string (NULL if none)
/// @param args       Process argument string (NULL if none)
/// @param sid        Process owner security ID string (NULL if none)
/// @param timestamp  Process start kernel timestamp (NULL for current time)
///
/// @returns The block if successful; NULL otherwise
__checkReturn
BLOCK_NODE* GetProcessBlock(
	__in const bool            started,
	__in const UINT32          pid,
	__in const UINT32          parentPid,
	__in UNICODE_STRING       *path,
	__in UNICODE_STRING       *args,
	__in UNICODE_STRING       *sid,
	__in const LARGE_INTEGER  *timestamp);

//----------------------------------------------------------------------------
/// @brief Gets the process ID associated with a connection ID
///
/// @param connectionId   ID of the connection
/// @param addressFamily  Address family for the connection (IPv4/IPv6)
/// @param protocol       Protocol for the connection (TCP/UDP)
/// @param port           Local port associated with the connection
///
/// @returns Process ID if successful; _UI32_MAX otherwise
UINT32 GetProcessIdForConnectionId(
	__in const UINT32 connectionId,
	__in const UINT16 addressFamily,
	__in const UINT8  protocol,
	__in const UINT16 port);

//----------------------------------------------------------------------------
/// @brief Gets the size of the ring buffer from the registry
///
/// * Minimum size is 1024 bytes
/// * Default size is four pages
/// * Maximum size is 32 pages
///
/// @returns Ring buffer size from registry successful; default size otherwise
__drv_requiresIRQL(PASSIVE_LEVEL)
UINT32 GetRingBufferSize(void);

//----------------------------------------------------------------------------
/// @brief Checks if the registry value is a valid system ID value
///
/// @param valueName     Name of the registry value
/// @param valueType     Type of the registry value
/// @param valueData     Data in the registry value
/// @param valueLength   Length of the data
/// @param context       Unused
/// @param entryContext  Destination buffer for the data
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
RTL_QUERY_REGISTRY_ROUTINE GetRingBufferSizeQueryRoutine;

//----------------------------------------------------------------------------
/// @brief Allocates and populates PCAP-NG section header block
///
/// The block's reference count is already set to 1
///
/// @returns The block if successful; NULL otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
BLOCK_NODE* GetSectionHeaderBlock(void);

//----------------------------------------------------------------------------
/// @brief Gets the current timestamp in PCAP-NG format
///
/// @param timestamp  Buffer to hold current timestamp
void GetTimestamp(__out LARGE_INTEGER *timestamp);

//----------------------------------------------------------------------------
/// @brief Holds a packet block until its connection event is received
///
/// @param blockNode  Packet block to hold
void HoldPacketBlock(__in BLOCK_NODE *blockNode);

//----------------------------------------------------------------------------
/// @brief Processes all deferred connection close events
///
/// @param dpc      DPC object associated with this routine
/// @param context  Unused
/// @param arg1     Unused
/// @param arg2     Unused
KDEFERRED_ROUTINE ProcessConnectionCloseEvents;

//----------------------------------------------------------------------------
/// @brief Releases all packet blocks for a connection
///
/// @param connectionId  ID of the connection
/// @param processId     ID of the process that owns the connection
void ReleasePacketBlocks(
	__in const UINT32 connectionId,
	__in const UINT32 processId);

//----------------------------------------------------------------------------
/// @brief Sets PCAP-NG option parameters and copies option data
///
/// @param buffer  Buffer to hold the option
/// @param offset  Offset to start of the option
/// @param code    Option code
/// @param data    Data to copy into the option
/// @param length  Length of data in bytes
///
/// @returns Offset to next byte after the option
UINT32 SetOption(
	__in char         *buffer,
	__in UINT32        offset,
	__in const UINT16  code,
	__in const void   *data,
	__in const UINT16  length);

//----------------------------------------------------------------------------
/// @brief Sets UTF-8 string PCAP-NG option parameters and copies option data
///
/// If bytesRemoved is not NULL, then the function converts the string to
/// argv-style argument list and stores the number of bytes removed.  If it is
/// NULL, the function does not perform this conversion.
///
/// @param buffer        Buffer to hold the option
/// @param offset        Offset to start of the option
/// @param code          Option code
/// @param data          Unicode string data to copy into the option
/// @param length        Length of data in bytes
/// @param bytesRemoved  Number of bytes removed when converting the
///
/// @returns Offset to next byte after the option
UINT32 SetUtf8Option(
	__in char                 *buffer,
	__in UINT32                offset,
	__in const UINT16          code,
	__in const UNICODE_STRING *data,
	__in UINT16                length,
	__in UINT16               *bytesRemoved);

//----------------------------------------------------------------------------
/// @brief Calculates seconds elapsed between start and end tick counts
///
/// @param start  Starting tick count
/// @param end    Ending tick count
///
/// @returns Seconds elapsed between start and end tick counts
UINT32 TickDiffToSeconds(const LARGE_INTEGER *start, const LARGE_INTEGER *end);

#ifdef __cplusplus
};
#endif

#endif  // QUEUE_MANAGER_PRIV_H
