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

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "queue_manager_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------

#pragma warning(push)
#pragma warning(disable:4706) // LLRB uses assignments in conditional expressions
LLRB_GENERATE(BlockTree, BLOCK_NODE, TreeEntry, CompareBlockNodes)
LLRB_GENERATE(OconnTree, OCONN_NODE, TreeEntry, CompareOconnNodes)
#pragma warning(pop)

LLRB_CLEAR_GENERATE(BlockTree, BLOCK_NODE, TreeEntry, QmCleanupBlock)
LLRB_CLEAR_GENERATE(OconnTree, OCONN_NODE, TreeEntry, CleanupOconnNode)

static BLOCK_TREE_HEAD     gConnTreeHead        = LLRB_INITIALIZER(&gConnTreeHead);      // Open connections
static OCONN_TREE_HEAD     gOconnTcp4TreeHead   = LLRB_INITIALIZER(&gOconnTcp4TreeHead); // Previously opened TCP/IPv4 connections
static OCONN_TREE_HEAD     gOconnTcp6TreeHead   = LLRB_INITIALIZER(&gOconnTcp6TreeHead); // Previously opened TCP/IPv6 connections
static OCONN_TREE_HEAD     gOconnUdp4TreeHead   = LLRB_INITIALIZER(&gOconnUdp4TreeHead); // Previously opened UDP/IPv4 connections
static OCONN_TREE_HEAD     gOconnUdp6TreeHead   = LLRB_INITIALIZER(&gOconnUdp6TreeHead); // Previously opened UDP/IPv6 connections
static BLOCK_TREE_HEAD     gPacketTreeHead      = LLRB_INITIALIZER(&gPacketTreeHead);    // Held packets
static BLOCK_TREE_HEAD     gProcessTreeHead     = LLRB_INITIALIZER(&gProcessTreeHead);   // Running processes

static LOOKASIDE_LIST_EX   gBlockNodeLal;                   // Holds memory for the block nodes
static bool                gBlockNodeLalInit    = false;    // True if lookaside list was initialized
static KDPC                gConnCloseDpc;                   // DPC to process connection close events
static KTIMER              gConnCloseTimer;                 // Timer to trigger processing of connection close events
static LARGE_INTEGER       gConnCloseTimeout;               // Timeout to use for connection close timer
static LIST_ENTRY          gConnCloseListHead   = {0};      // Head of list of closed connections
static UINT16              gConnTreeCount       = 0;        // Number of open connections
static LARGE_INTEGER       gDriverLoadTick      = {0};      // Tick count when driver loaded
static LOOKASIDE_LIST_EX   gOconnNodeLal;                   // Holds memory for the open connection nodes
static bool                gOconnNodeLalInit    = false;    // True if lookaside list was initialized
static UINT16              gPacketTreeCount     = 0;        // Number of held packets
static const UINT32        gPoolTagBlockNode    = 'bQoH';   // Tag to use when allocating block nodes from lookaside list
static const UINT32        gPoolTagConnection   = 'cQoH';   // Tag to use when allocating connection block buffers
static const UINT32        gPoolTagInterface    = 'iQoH';   // Tag to use when allocating interface description block buffers
static const UINT32        gPoolTagPacket       = 'kQoH';   // Tag to use when allocating packet block buffers
static const UINT32        gPoolTagOconnNode    = 'oQoH';   // Tag to use when allocating open connection nodes from lookaside list
static const UINT32        gPoolTagProcess      = 'pQoH';   // Tag to use when allocating process block buffers
static const UINT32        gPoolTagRingBuffer   = 'rQoH';   // Tag to use when allocating initial blocks ring buffer
static const UINT32        gPoolTagSection      = 'sQoH';   // Tag to use when allocating section header block buffers
static UINT16              gProcessTreeCount    = 0;        // Number of running processes
static LIST_ENTRY          gReaderListHead      = {0};      // Head of list of registered readers
static KSPIN_LOCK          gReaderListLock;                 // Locks list of registered readers
static LARGE_INTEGER       gReaderTick          = {0};      // Tick count when first register registered
static BLOCK_NODE         *gSectionHeaderBlock  = NULL;     // PCAP-NG section header block
static STATISTICS          gStatistics    = {HONE_VERSION}; // Driver statistics;
static const LONGLONG      gTimestampConv = 11644473600;    // Number of seconds between 1/1/1601 and 1/1/1970
static KSPIN_LOCK          gTreesLock;                      // Locks connection and process LLRB trees

// Ring buffer size registry key and value
static wchar_t *gBufferSizeKeyPath   = L"\\Registry\\Machine\\SOFTWARE\\PNNL\\Hone";
static wchar_t *gBufferSizeValueName = L"RingBufferSize";

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE* AllocateBlockNode(
	__in const UINT32 blockLength,
	__in const UINT32 poolTag)
{
	BLOCK_NODE *blockNode = reinterpret_cast<BLOCK_NODE*>(
			ExAllocateFromLookasideListEx(&gBlockNodeLal));
	if (!blockNode) {
		return NULL;
	}

	// Zero block node header, but not the data
	RtlZeroMemory(blockNode, sizeof(BLOCK_NODE) - sizeof(blockNode->Data));

	// Allocate separate data buffer if the block node isn't large enough to
	// hold all of the data
	if (blockLength > sizeof(blockNode->Data)) {
		blockNode->Buffer = reinterpret_cast<char*>(ExAllocatePoolWithTag(
				NonPagedPool, blockLength, poolTag));
		if (!blockNode->Buffer) {
			ExFreeToLookasideListEx(&gBlockNodeLal, blockNode);
			return NULL;
		}
	}

	blockNode->RefCount    = 1; // Hold a reference to the block
	blockNode->BlockLength = blockLength;
	return blockNode;
}

//----------------------------------------------------------------------------
void CalculateMaxSnapLength(void)
{
	LIST_ENTRY *entry      = gReaderListHead.Flink;
	UINT32      maxSnapLen = 0;

	while (entry != &gReaderListHead) {
		const READER_INFO *reader = CONTAINING_RECORD(entry, READER_INFO, ListEntry);
		if ((reader->SnapLength == 0) || (reader->SnapLength == _UI32_MAX)) {
			maxSnapLen = _UI32_MAX;
			break;
		} else if (reader->SnapLength > maxSnapLen) {
			maxSnapLen = reader->SnapLength;
		}
		entry = entry->Flink;
	}
	gStatistics.MaxSnapLength = maxSnapLen;
}

//----------------------------------------------------------------------------
void CleanupOconnNode(__in OCONN_NODE *oconnNode)
{
	if (oconnNode) {
		ExFreeToLookasideListEx(&gOconnNodeLal, oconnNode);
	}
}

//----------------------------------------------------------------------------
void CleanupReader(__in READER_INFO *reader)
{
	if (reader->BlocksBuffer.Buffer) {
		CleanupRingBuffer(&reader->BlocksBuffer);
		ExFreePool(reader->BlocksBuffer.Buffer);
	}
	if (reader->InitialBuffer.Buffer) {
		CleanupRingBuffer(&reader->InitialBuffer);
		ExFreePool(reader->InitialBuffer.Buffer);
	}
	if (reader->DataEvent) {
		ObDereferenceObject(reader->DataEvent);
	}
}

//----------------------------------------------------------------------------
void CleanupRingBuffer(__in RING_BUFFER *buffer)
{
	if (buffer) {
		while (!IsRingBufferEmpty(buffer)) {
			BLOCK_NODE *blockNode = reinterpret_cast<BLOCK_NODE*>(
					RingBufferDequeue(buffer));
			QmCleanupBlock(blockNode);
		}
		buffer->Front = 0;
		buffer->Back  = 0;
	}
}

//----------------------------------------------------------------------------
int CompareBlockNodes(BLOCK_NODE *first, BLOCK_NODE *second)
{
	return first->PrimaryId - second->PrimaryId;
}

//----------------------------------------------------------------------------
int CompareOconnNodes(OCONN_NODE *first, OCONN_NODE *second)
{
	return first->Port - second->Port;
}

//----------------------------------------------------------------------------
UINT16 ConvertCommandLineToArgv(__in char *buffer, __in const UINT16 length)
{
	UINT16  inputIndex  = 0;
	UINT16  outputIndex = 0;
	bool    inQuote     = false;

	if (length == 0) {
		return 0;
	}

	// Parse first argument (program filename)
	// If it starts with a double quote, it ends at the next double quote
	// Otherwise, it ends at the first tab, space, or newline character
	// Treat all other characters in the first argument literally
	if (buffer[0] == '"') {
		inQuote = true;
		inputIndex++;
	}
	while (inputIndex < length) {
		if ((inQuote && (buffer[inputIndex] == '"')) ||
				(!inQuote && ((buffer[inputIndex] == ' ') ||
				(buffer[inputIndex] == '\t') ||
				(buffer[inputIndex] == '\n')))) {
			inQuote = false;
			buffer[outputIndex++] = '\0';
			inputIndex++;
			break;
		}
		buffer[outputIndex++] = buffer[inputIndex++];
	}

	// Parse remaining arguments
	while (inputIndex < length) {
		bool inArg = true;

		// Skip spaces and tabs
		while ((inputIndex < length) &&
				((buffer[inputIndex] == ' ') || (buffer[inputIndex] == '\t'))) {
			inputIndex++;
		}
		if (inputIndex >= length) {
			break;
		}

		// Parse the current argument
		while (inArg && (inputIndex < length)) {
			UINT16  backslashes = 0;
			bool    skipChar    = false;

			// Count the number of backslashes
			while ((inputIndex < length) && (buffer[inputIndex] == '\\')) {
				backslashes++;
				inputIndex++;
			}

			// Write out the backslashes if at the end of the argument string
			if (inputIndex >= length) {
				while (backslashes > 0) {
					buffer[outputIndex++] = '\\';
					backslashes--;
				}
				inArg = false;
				break;
			}

			// Check if next character is a double quote
			if (buffer[inputIndex] == '"') {
				// Check if this double quote follows an even number of backslashes
				if ((backslashes % 2) == 0) {
					// Check if we are currently in a double-quoted part
					if (inQuote) {
						// This double quote marks the end of a double-quoted part
						// If the next character is also a double quote, move to it
						// Otherwise, skip this double quote
						inQuote = false;
						if ((inputIndex + 1 < length) && buffer[inputIndex+1] == '"') {
							inputIndex++;
						} else {
							skipChar = true;
						}
					} else {
						// This double quote marks the start of a double-quoted part, so
						// skip this double quote
						inQuote  = true;
						skipChar = true;
					}
				}

				// Divide the number of preceding backslashes by two, since they are
				// followed by a double quote
				backslashes /= 2;
			} else {
				// If we're not in a double-quoted part, a space or tab character marks
				// the end of the argument
				if (!inQuote &&
						((buffer[inputIndex] == ' ') || (buffer[inputIndex] == '\t'))) {
					// Skip this character, since we'll be replacing it with a null
					inArg    = false;
					skipChar = true;
				}
			}

			// Write out backslashes
			while (backslashes > 0) {
				buffer[outputIndex++] = '\\';
				backslashes--;
			}

			// Copy the character, unless we're skipping it
			if (!skipChar) {
				buffer[outputIndex++] = buffer[inputIndex];
			}
			inputIndex++;
		}

		// Mark end of argument with a null character
		buffer[outputIndex++] = '\0';
	}

	// Make sure string is null terminated
	if ((outputIndex == 0) ||
			((outputIndex > 0) && (buffer[outputIndex - 1] != '\0'))) {
		buffer[outputIndex++] = '\0';
	}

	return outputIndex;
}

//----------------------------------------------------------------------------
void ConvertKeTime(__in const LARGE_INTEGER *in, __out LARGE_INTEGER *out)
{
	LARGE_INTEGER perfCounterTime;
	LARGE_INTEGER timeFreq;
	LONG          sec;
	LONG          usec;

	perfCounterTime = KeQueryPerformanceCounter(&timeFreq);
	sec             = (LONG)(in->QuadPart / 10000000 - gTimestampConv);
	usec            = (LONG)((in->QuadPart % 10000000) / 10);
	sec            -= (ULONG)(perfCounterTime.QuadPart / timeFreq.QuadPart);
	usec           -= (LONG)((perfCounterTime.QuadPart % timeFreq.QuadPart)
			* 1000000 / timeFreq.QuadPart);
	out->QuadPart = static_cast<LONGLONG>(sec) * 1000000 + usec;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS DeinitializeQueueManager(__in void)
{
	KLOCK_QUEUE_HANDLE  lockHandle;
	LIST_ENTRY         *entry;

	KeCancelTimer(&gConnCloseTimer);

	entry = gReaderListHead.Flink;
	while (entry != &gReaderListHead) {
		READER_INFO *reader = CONTAINING_RECORD(entry, READER_INFO, ListEntry);
		entry = entry->Flink;
		CleanupReader(reader);
	}

	entry = gConnCloseListHead.Flink;
	while (entry != &gConnCloseListHead) {
		BLOCK_NODE *blockNode = CONTAINING_RECORD(entry, BLOCK_NODE, ListEntry);
		entry = entry->Flink;
		QmCleanupBlock(blockNode);
	}

	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
	LLRB_CLEAR(BlockTree, &gConnTreeHead);
	LLRB_CLEAR(BlockTree, &gPacketTreeHead);
	LLRB_CLEAR(BlockTree, &gProcessTreeHead);
	LLRB_CLEAR(OconnTree, &gOconnTcp4TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnTcp6TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnUdp4TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnUdp6TreeHead);
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);

	if (gBlockNodeLalInit) {
		ExDeleteLookasideListEx(&gBlockNodeLal);
	}
	if (gOconnNodeLalInit) {
		ExDeleteLookasideListEx(&gOconnNodeLal);
	}

	QmCleanupBlock(gSectionHeaderBlock);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
void EnqueueBlock(__in BLOCK_NODE *blockNode)
{
	KLOCK_QUEUE_HANDLE  lockHandle;
	LIST_ENTRY         *entry = gReaderListHead.Flink;

	if (!gStatistics.NumReaders) {
		return;
	}

	DBGPRINT(D_LOCK, "Acquiring reader list lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gReaderListLock, &lockHandle);

	// Increment packet counts inside the spin lock
	if (blockNode->BlockType == PacketBlock) {
		char                  *buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
		PCAP_NG_PACKET_HEADER *header = reinterpret_cast<PCAP_NG_PACKET_HEADER*>(buffer);

		gStatistics.CapturedPackets++;
		gStatistics.CapturedPacketBytes += header->CapturedLength;
	}

	while (entry != &gReaderListHead) {
		READER_INFO *reader = CONTAINING_RECORD(entry, READER_INFO, ListEntry);
		const bool empty = IsRingBufferEmpty(&reader->BlocksBuffer);
		InterlockedIncrement(&blockNode->RefCount);
		if (RingBufferEnqueue(&reader->BlocksBuffer, blockNode)) {
			// Only signal the reader if the buffer was empty
			if (empty && reader->DataEvent) {
				KeSetEvent(reader->DataEvent, 1, FALSE);
			}
		} else {
			InterlockedDecrement(&blockNode->RefCount);
		}
		entry = entry->Flink;
	}

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released reader list lock at %d", __LINE__);
}

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE* GetConnectionBlock(
	__in const bool           opened,
	__in const UINT32         connectionId,
	__in const UINT32         processId,
	__in const LARGE_INTEGER *timestamp)
{
	BLOCK_NODE                *blockNode;
	char                      *buffer;
	PCAP_NG_CONNECTION_HEADER *header;
	UINT32                     blockLength;
	UINT32                     blockOffset;
	static const UINT32        connectionClosedEvent = 0xFFFFFFFF;

	blockLength = sizeof(PCAP_NG_CONNECTION_HEADER) + sizeof(UINT32);
	if (!opened) {
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) +
				sizeof(connectionClosedEvent) + sizeof(PCAP_NG_OPTION_HEADER);
	}
	blockNode = AllocateBlockNode(blockLength, gPoolTagConnection);
	if (!blockNode) {
		return NULL;
	}

	blockNode->BlockType   = ConnectionBlock;
	blockNode->PrimaryId   = connectionId;
	blockNode->SecondaryId = processId;

	// Use the supplied timestamp, even if it is zero (which may be the case
	// when the timestamp was supplied in a list of open connections)
	if (timestamp) {
		ConvertKeTime(timestamp, &blockNode->Timestamp);
	} else {
		GetTimestamp(&blockNode->Timestamp);
	}

	buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
	header = reinterpret_cast<PCAP_NG_CONNECTION_HEADER*>(buffer);
	header->BlockType     = blockNode->BlockType;
	header->BlockLength   = blockNode->BlockLength;
	header->ConnectionId  = connectionId;
	header->ProcessId     = processId;
	header->TimestampHigh = blockNode->Timestamp.HighPart;
	header->TimestampLow  = blockNode->Timestamp.LowPart;
	blockOffset = sizeof(PCAP_NG_CONNECTION_HEADER);
	if (!opened) {
		blockOffset = SetOption(buffer, blockOffset, 2, &connectionClosedEvent,
				sizeof(connectionClosedEvent));
		RtlZeroMemory(buffer + blockOffset, sizeof(PCAP_NG_OPTION_HEADER)); // End
	}
	*reinterpret_cast<UINT32*>(buffer + blockNode->BlockLength - sizeof(UINT32)) =
			blockNode->BlockLength;
	return blockNode;
}

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE* GetInterfaceDescriptionBlock(void)
{
	BLOCK_NODE                    *blockNode;
	char                          *buffer;
	PCAP_NG_INTERFACE_DESCRIPTION *block;
	static const char             *ifdesc = "Hone Capture Pseudo-device\0\0";

	blockNode = AllocateBlockNode(sizeof(PCAP_NG_INTERFACE_DESCRIPTION),
			gPoolTagInterface);
	if (!blockNode) {
		return NULL;
	}

	blockNode->BlockType = InterfaceDescriptionBlock;
	buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
	block  = reinterpret_cast<PCAP_NG_INTERFACE_DESCRIPTION*>(buffer);
	block->BlockType                 = blockNode->BlockType;
	block->BlockLength               = blockNode->BlockLength;
	block->LinkType                  = 101; // LINKTYPE_RAW
	block->Reserved                  = 0;
	block->SnapLength                = 0;
	block->IfDescHeader.OptionCode   = 3;
	block->IfDescHeader.OptionLength = sizeof(block->IfDesc);
	block->OptionEnd.OptionCode      = 0;
	block->OptionEnd.OptionLength    = 0;
	block->BlockLengthFooter         = blockNode->BlockLength;
	RtlCopyMemory(block->IfDesc, ifdesc, sizeof(block->IfDesc));
	return blockNode;
}

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE* GetProcessBlock(
	__in const bool           started,
	__in const UINT32         pid,
	__in const UINT32         parentPid,
	__in UNICODE_STRING      *path,
	__in UNICODE_STRING      *args,
	__in UNICODE_STRING      *sid,
	__in const LARGE_INTEGER *timestamp)
{
	BLOCK_NODE             *blockNode;
	char                   *buffer;
	PCAP_NG_PROCESS_HEADER *header;
	UINT32                  blockLength;
	ULONG                   pathLength        = 0;
	ULONG                   argsLength        = 0;
	ULONG                   argvLength        = 0;
	ULONG                   sidLength         = 0;
	UINT16                  bytesRemoved      = 0;
	UINT16                  optionsCount      = 0;
	static const UINT32     processEndedEvent = 0xFFFFFFFF;

	blockLength = sizeof(PCAP_NG_PROCESS_HEADER) + sizeof(UINT32);
	if (!started) {
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) + sizeof(processEndedEvent);
		optionsCount++;
	}
	if (path && path->Buffer && path->Length) {
		RtlUnicodeToUTF8N(NULL, 0, &pathLength, path->Buffer, path->Length);
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(pathLength);
		optionsCount++;
	}
	if (args && args->Buffer && args->Length) {
		// Since we store the arguments as an null-sparated array (like Unix), and
		// as an unprocessed string, we need to reserve space for both
		RtlUnicodeToUTF8N(NULL, 0, &argsLength, args->Buffer, args->Length);
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(argsLength);
		argvLength = argsLength;
		if (args->Buffer[(args->Length/2)-1] != L'\0') {
			argvLength++; // Add one byte for NULL terminator if necessary
		}
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(argvLength);
		optionsCount += 2;
	}
	if (sid && sid->Buffer && sid->Length) {
		RtlUnicodeToUTF8N(NULL, 0, &sidLength, sid->Buffer, sid->Length);
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(sidLength);
		optionsCount++;
	}
	if (optionsCount) {
		blockLength += sizeof(PCAP_NG_OPTION_HEADER);
	}

	blockNode = AllocateBlockNode(blockLength, gPoolTagProcess);
	if (!blockNode) {
		return NULL;
	}

	blockNode->BlockType = ProcessBlock;
	blockNode->PrimaryId = pid;
	if (timestamp) {
		ConvertKeTime(timestamp, &blockNode->Timestamp);
	} else {
		GetTimestamp(&blockNode->Timestamp);
	}
	buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
	header = reinterpret_cast<PCAP_NG_PROCESS_HEADER*>(buffer);
	header->BlockType                    = blockNode->BlockType;
	header->ProcessId                    = pid;
	header->TimestampHigh                = blockNode->Timestamp.HighPart;
	header->TimestampLow                 = blockNode->Timestamp.LowPart;
	header->ParentPidHeader.OptionCode   = 5;
	header->ParentPidHeader.OptionLength = sizeof(header->ParentPid);
	header->ParentPid                    = parentPid;
	if (optionsCount) {
		UINT32 blockOffset = sizeof(PCAP_NG_PROCESS_HEADER);
		if (!started) {
			blockOffset = SetOption(buffer, blockOffset, 2, &processEndedEvent,
			sizeof(processEndedEvent));
		}
		blockOffset = SetUtf8Option(buffer, blockOffset, 3, path,
				static_cast<UINT16>(pathLength), NULL);
		blockOffset = SetUtf8Option(buffer, blockOffset, 4, args,  // Parsed args
				static_cast<UINT16>(argvLength), &bytesRemoved);
		blockOffset = SetUtf8Option(buffer, blockOffset, 11, args, // Raw args
				static_cast<UINT16>(argsLength), NULL);
		blockOffset = SetUtf8Option(buffer, blockOffset, 10, sid,
				static_cast<UINT16>(sidLength),  NULL);
		RtlZeroMemory(buffer + blockOffset, sizeof(PCAP_NG_OPTION_HEADER)); // End
	}

	// Adjust the length since it may have shrunk when parsing the argument list
	blockNode->BlockLength = blockLength - bytesRemoved;
	header->BlockLength    = blockNode->BlockLength;
	*reinterpret_cast<UINT32*>(buffer + blockNode->BlockLength - sizeof(UINT32)) =
			blockNode->BlockLength;
	return blockNode;
}

//----------------------------------------------------------------------------
UINT32 GetProcessIdForConnectionId(
	__in const UINT32 connectionId,
	__in const UINT16 addressFamily,
	__in const UINT8  protocol,
	__in const UINT16 port)
{
	UINT32              processId;
	BLOCK_NODE         *blockNode;
	BLOCK_NODE          searchNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	searchNode.PrimaryId = connectionId;
	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);

	blockNode = LLRB_FIND(BlockTree, &gConnTreeHead, &searchNode);
	if (blockNode) {
		processId = blockNode->SecondaryId;
	} else {
		// Try to find the connection in the previously opened connections trees
		OCONN_TREE_HEAD *treeHead;
		OCONN_NODE      *oconnNode;
		OCONN_NODE       searchNode;

		if (addressFamily == AF_INET) {
			if (protocol == IPPROTO_TCP) {
				treeHead = &gOconnTcp4TreeHead;
			} else {
				treeHead = &gOconnUdp4TreeHead;
			}
		} else {
			if (protocol == IPPROTO_TCP) {
				treeHead = &gOconnTcp6TreeHead;
			} else {
				treeHead = &gOconnUdp6TreeHead;
			}
		}

		searchNode.Port = port;
		oconnNode = LLRB_FIND(OconnTree, treeHead, &searchNode);
		if (oconnNode) {
			// Cache this open connection now that we have a mapping between the
			// connection ID and the process ID
			BLOCK_NODE *blockNode;

			processId = oconnNode->ProcessId;
			blockNode = GetConnectionBlock(true, connectionId, processId,
				&oconnNode->Timestamp);
			if (blockNode) {
				LLRB_INSERT(BlockTree, &gConnTreeHead, blockNode);
				EnqueueBlock(blockNode);
			}
		} else {
			processId = _UI32_MAX;
		}
	}

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
	return processId;
}

//----------------------------------------------------------------------------
__drv_requiresIRQL(PASSIVE_LEVEL)
UINT32 GetRingBufferSize(void)
{
	UINT32                    bufferSize;
	NTSTATUS                  status;
	RTL_QUERY_REGISTRY_TABLE  queryTable[2] = {0};

	// Get the system ID if possible
	queryTable[0].QueryRoutine = GetRingBufferSizeQueryRoutine;
	queryTable[0].Flags        = RTL_QUERY_REGISTRY_REQUIRED;
	queryTable[0].Name         = gBufferSizeValueName;
	queryTable[0].EntryContext = &bufferSize;
	queryTable[0].DefaultType  = REG_NONE;

	status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, gBufferSizeKeyPath,
			queryTable, NULL, NULL);
	if (!NT_SUCCESS(status) || (bufferSize == 0)) {
		return PAGE_SIZE << 2;
	}
	if (bufferSize < 1024) {
		return 1024;
	}
	if (bufferSize > (PAGE_SIZE << 5)) {
		return PAGE_SIZE << 5;
	}

	// Round up to next power of 2
	// http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
	bufferSize--;
	bufferSize |= bufferSize >> 1;
	bufferSize |= bufferSize >> 2;
	bufferSize |= bufferSize >> 4;
	bufferSize |= bufferSize >> 8;
	bufferSize |= bufferSize >> 16;
	bufferSize++;
	return bufferSize;
}

//----------------------------------------------------------------------------
NTSTATUS GetRingBufferSizeQueryRoutine(
	__in wchar_t       *valueName,
	__in unsigned long  valueType,
	__in void          *valueData,
	__in unsigned long  valueLength,
	__in void          *context,
	__in void          *entryContext)
{
	UNREFERENCED_PARAMETER(context);
	if (valueName && valueData && entryContext &&
			(0 == wcscmp(valueName, gBufferSizeValueName)) &&
			(valueType == REG_DWORD) && (valueLength >= sizeof(UINT32))) {
		RtlCopyMemory(entryContext, valueData, sizeof(UINT32));
		return STATUS_SUCCESS;
	}
	return STATUS_OBJECT_NAME_NOT_FOUND;
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
BLOCK_NODE* GetSectionHeaderBlock(void)
{
	// Unique system identifier
	struct SYSTEM_ID {
		UINT8 Type;   // Identifier type
		UINT8 Pad[3]; // Padding
		GUID  Id;     // The identifier
	};

	NTSTATUS                status;
	char                   *buffer;
	PCAP_NG_SECTION_HEADER *header;
	BLOCK_NODE             *blockNode;
	UINT32                  blockLength;
	UINT32                  blockOffset;
	static const UINT8      bits = (sizeof(void*) == 4) ? 32 : 64;
	char                    application[32];
	UINT16                  applicationLen;
	char                    hardware[32];
	UINT16                  hardwareLen;
	char                    os[80];
	UINT16                  osLen;
	SYSTEM_ID               systemId = {0};
	UINT16                  systemIdLen;
	RTL_OSVERSIONINFOEXW    versionInfo = {0};

	versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
	status = RtlGetVersion(reinterpret_cast<RTL_OSVERSIONINFOW*>(&versionInfo));
	if (NT_SUCCESS(status)) {
		char   *str       = os;
		size_t  remaining = sizeof(os);
		char   *osName    = NULL;
		if (versionInfo.dwMajorVersion == 6) {
			switch (versionInfo.dwMinorVersion) {
			case 1:
				osName = (versionInfo.wProductType == 1) ?
						"Windows 7" : "Windows Server 2008 R2";
				break;
			case 2:
				osName = (versionInfo.wProductType == 1) ?
						"Windows 8" : "Windows Server 2012";
				break;
			case 3:
				osName = (versionInfo.wProductType == 1) ?
						"Windows 8.1" : "Windows Server 2012 R2";
			default:
				break;
			}
		}

		if (osName) {
			RtlCopyMemory(str, osName, ::strlen(osName));
			remaining -= ::strlen(osName);
			str       += ::strlen(osName);
		} else {
			RtlStringCbPrintfExA(str, remaining, &str, &remaining, 0, "NT %d.%d",
					versionInfo.dwMajorVersion, versionInfo.dwMinorVersion);
		}

		if (versionInfo.wServicePackMajor != 0) {
			RtlStringCbPrintfExA(str, remaining, &str, &remaining, 0,
					" Service Pack %d", versionInfo.wServicePackMajor);
			if (versionInfo.wServicePackMinor != 0) {
				RtlStringCbPrintfExA(str, remaining, &str, &remaining, 0,
						".%d", versionInfo.wServicePackMinor);
			}
		}

		RtlStringCbPrintfExA(str, remaining, &str, &remaining, 0,
				" %d-bit, Build %d", bits, versionInfo.dwBuildNumber);

		RtlStringCbPrintfA(hardware, sizeof(hardware),
				"%d-bit x86 %s", bits, (versionInfo.wProductType == 1) ?
				"workstation" : "server");
	} else {
		static const char *osName = "Unknown Windows NT version";
		RtlCopyMemory(os, osName, ::strlen(osName));
		RtlStringCbPrintfA(hardware, sizeof(hardware),
				"%d-bit x86 workstation", bits);
	}

	RtlStringCbPrintfA(application, sizeof(application),
			"HONE %s", HONE_PRODUCTVERSION_STR);

	status = GetSystemId(&systemId.Id);
	if (NT_SUCCESS(status)) {
		systemId.Type = 1; // Indicates that ID is a binary GUID
		systemIdLen   = sizeof(systemId);
	} else {
		systemIdLen = 0;
	}

	applicationLen = static_cast<UINT16>(::strlen(application));
	hardwareLen    = static_cast<UINT16>(::strlen(hardware   ));
	osLen          = static_cast<UINT16>(::strlen(os         ));
	blockLength    = sizeof(PCAP_NG_SECTION_HEADER) +
			sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(hardwareLen   ) +
			sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(osLen         ) +
			sizeof(PCAP_NG_OPTION_HEADER) + PCAP_NG_PADDING(applicationLen) +
			sizeof(PCAP_NG_OPTION_HEADER) + sizeof(UINT32);
	if (systemIdLen) {
		blockLength += sizeof(PCAP_NG_OPTION_HEADER) +
				PCAP_NG_PADDING(systemIdLen);
	}
	blockNode = AllocateBlockNode(blockLength, gPoolTagSection);
	if (!blockNode) {
		return NULL;
	}

	blockNode->BlockType = SectionHeaderBlock;
	buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
	header = reinterpret_cast<PCAP_NG_SECTION_HEADER*>(buffer);
	header->BlockType     = blockNode->BlockType;
	header->BlockLength   = blockNode->BlockLength;
	header->ByteOrder     = 0x1A2B3C4D;
	header->MajorVersion  = 1;
	header->MinorVersion  = 0;
	header->SectionLength = _UI64_MAX;
	blockOffset = sizeof(PCAP_NG_SECTION_HEADER);
	blockOffset = SetOption(buffer, blockOffset, 2, hardware,    hardwareLen);
	blockOffset = SetOption(buffer, blockOffset, 3, os,          osLen);
	blockOffset = SetOption(buffer, blockOffset, 4, application, applicationLen);
	blockOffset = SetOption(buffer, blockOffset, 257, &systemId, systemIdLen);
	RtlZeroMemory(buffer + blockOffset, sizeof(PCAP_NG_OPTION_HEADER)); // End
	*reinterpret_cast<UINT32*>(buffer + blockNode->BlockLength - sizeof(UINT32)) =
			blockNode->BlockLength;
	return blockNode;
}

//----------------------------------------------------------------------------
void GetTimestamp(__out LARGE_INTEGER *timestamp)
{
	LARGE_INTEGER systemTime;

	KeQuerySystemTime(&systemTime);
	timestamp->QuadPart = systemTime.QuadPart / 10 - gTimestampConv * 1000000;
}

//----------------------------------------------------------------------------
void HoldPacketBlock(__in BLOCK_NODE *blockNode)
{
	BLOCK_NODE         *existing;
	KLOCK_QUEUE_HANDLE  lockHandle;

	DBGPRINT(D_INFO, "Holding packet block for connection %08X",
			blockNode->PrimaryId);

	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
	InterlockedIncrement(&blockNode->RefCount);
	existing = LLRB_INSERT(BlockTree, &gPacketTreeHead, blockNode);
	if (existing) {
		// Insert this block in the list
		InsertTailList(&existing->ListEntry, &blockNode->ListEntry);
	} else {
		InitializeListHead(&blockNode->ListEntry);
	}
	gPacketTreeCount++;
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS InitializeQueueManager(DEVICE_OBJECT *device)
{
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(device);

	KeQueryTickCount(&gDriverLoadTick);
	InitializeListHead(&gReaderListHead);
	InitializeListHead(&gConnCloseListHead);

	status = ExInitializeLookasideListEx(&gBlockNodeLal, NULL, NULL,
			NonPagedPool, 0, sizeof(BLOCK_NODE), gPoolTagBlockNode, 0);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create block node lookaside list");
		return status;
	}
	gBlockNodeLalInit = true;

	status = ExInitializeLookasideListEx(&gOconnNodeLal, NULL, NULL,
			NonPagedPool, 0, sizeof(BLOCK_NODE), gPoolTagOconnNode, 0);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create open connection node lookaside list");
		return status;
	}
	gOconnNodeLalInit = true;

	KeInitializeDpc(&gConnCloseDpc, ProcessConnectionCloseEvents, NULL);
	KeInitializeTimer(&gConnCloseTimer);
	gConnCloseTimeout.QuadPart = -10000;

	KeInitializeSpinLock(&gReaderListLock);
	KeInitializeSpinLock(&gTreesLock);
	return status;
}

//----------------------------------------------------------------------------
void ProcessConnectionCloseEvents(
	__in     KDPC *dpc,
	__in_opt void *context,
	__in_opt void *arg1,
	__in_opt void *arg2)
{
	UNREFERENCED_PARAMETER(dpc);
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(arg1);
	UNREFERENCED_PARAMETER(arg2);

	KLOCK_QUEUE_HANDLE  lockHandle;
	LIST_ENTRY         *entry;
	LARGE_INTEGER       timestamp;

	GetTimestamp(&timestamp);

	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);

	entry = gConnCloseListHead.Flink;
	while (entry != &gConnCloseListHead) {
		BLOCK_NODE *blockNode = CONTAINING_RECORD(entry, BLOCK_NODE, ListEntry);

		entry = entry->Flink;
		if (timestamp.QuadPart > (blockNode->Timestamp.QuadPart + 1000)) {
			// This connection is old enough that we can remove it from the tree
			DBGPRINT(D_INFO, "Removing closed connection %08X", blockNode->PrimaryId);
			if (LLRB_REMOVE(BlockTree, &gConnTreeHead, blockNode)) {
				gConnTreeCount--;
			}
			InterlockedDecrement(&gStatistics.NumConnections);
			RemoveEntryList(&blockNode->ListEntry);
			QmCleanupBlock(blockNode);
		}
	}

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
}

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE *QmAllocatePacketBlock(
	__in const UINT32   dataLength,
	__in char         **dataBuffer)
{
	UINT32      blockLength;
	BLOCK_NODE *blockNode;

	blockLength = sizeof(PCAP_NG_PACKET_HEADER) + PCAP_NG_PADDING(dataLength) +
			sizeof(PCAP_NG_PACKET_FOOTER);
	blockNode = AllocateBlockNode(blockLength, gPoolTagPacket);
	if (!blockNode) {
		return NULL;
	}
	*dataBuffer = (blockNode->Buffer ? blockNode->Buffer : blockNode->Data) +
			sizeof(PCAP_NG_PACKET_HEADER);
	return blockNode;
}

//----------------------------------------------------------------------------
bool QmCleanupBlock(__in BLOCK_NODE *blockNode)
{
	bool freed = false;
	if (blockNode) {
		const LONG refCount = InterlockedDecrement(&blockNode->RefCount);
		if (refCount == 0) { // Free memory if reference count is 0
			if (blockNode->Buffer) {
				ExFreePool(blockNode->Buffer);
			}
			ExFreeToLookasideListEx(&gBlockNodeLal, blockNode);
			freed = true;
		}
	}
	return freed;
}

//----------------------------------------------------------------------------
__checkReturn
BLOCK_NODE* QmDequeueBlock(__in READER_INFO *reader)
{
	BLOCK_NODE *blockNode = NULL;

	// No need to lock, since we're using a lock-free ring buffer
	if (reader) {
		if (reader->InitialBuffer.Buffer) {
			blockNode = reinterpret_cast<BLOCK_NODE*>(
					RingBufferDequeue(&reader->InitialBuffer));
			if (IsRingBufferEmpty(&reader->InitialBuffer)) {
				ExFreePool(reader->InitialBuffer.Buffer);
				reader->InitialBuffer.Buffer = NULL;
			}
		} else {
			blockNode = reinterpret_cast<BLOCK_NODE*>(
					RingBufferDequeue(&reader->BlocksBuffer));
		}
	}
	return blockNode;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmDeregisterReader(__in READER_INFO *reader)
{
	KLOCK_QUEUE_HANDLE  lockHandle;

	if (!reader) {
		return STATUS_INVALID_PARAMETER;
	}

	DBGPRINT(D_LOCK, "Acquiring reader list lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gReaderListLock, &lockHandle);

	gStatistics.NumReaders--;
	if (gStatistics.NumReaders == 0) {
		LARGE_INTEGER tickCount;
		KeQueryTickCount(&tickCount);
		gStatistics.LoggingTime += TickDiffToSeconds(&gReaderTick, &tickCount);
		gReaderTick.QuadPart     = 0;
	}
	DBGPRINT(D_INFO, "Deregistered reader %d, total registered readers %d",
			reader->Id, gStatistics.NumReaders);
	CleanupReader(reader);
	RemoveEntryList(&reader->ListEntry);
	CalculateMaxSnapLength();

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released reader list lock at %d", __LINE__);

	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmEnqueueConnectionBlock(
	__in const bool   opened,
	__in const UINT32 connectionId,
	__in const UINT32 processId)
{
	BLOCK_NODE         *blockNode = NULL;
	BLOCK_NODE          searchNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	// Release packet blocks held for this connection
	ReleasePacketBlocks(connectionId, processId);

	// If connection opened, get the block node, if one already exists
	// If connection closed, set timer to delete the block node, if one exists
	searchNode.PrimaryId = connectionId;
	if (opened) {
		DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
		KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
		blockNode = LLRB_FIND(BlockTree, &gConnTreeHead, &searchNode);
		KeReleaseInStackQueuedSpinLock(&lockHandle);
		DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		if (blockNode) {
			return STATUS_SUCCESS; // Already enqueued open block for this connection
		}
		InterlockedIncrement(&gStatistics.ConnectionOpenEvents);
		InterlockedIncrement(&gStatistics.NumConnections);
	} else {
		bool held = false;
		DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
		KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
		blockNode = LLRB_FIND(BlockTree, &gConnTreeHead, &searchNode);
		if (blockNode && (blockNode->ListEntry.Flink == 0)) {
			// Hold the connection block for one second in case more packets arrive
			DBGPRINT(D_INFO, "Holding closed connection %08X for 1 second", connectionId);
			InsertTailList(&gConnCloseListHead, &blockNode->ListEntry);
			KeSetTimer(&gConnCloseTimer, gConnCloseTimeout, &gConnCloseDpc);
			held = true;
		}
		KeReleaseInStackQueuedSpinLock(&lockHandle);
		DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		if (blockNode && !held) {
			return STATUS_SUCCESS; // Already enqueued close block for this connection
		}
		blockNode = NULL; // So we don't free the block in the code below
		InterlockedIncrement(&gStatistics.ConnectionCloseEvents);
	}

	// Create a block if there are readers or need to save connection information
	if (gStatistics.NumReaders || opened) {
		blockNode = GetConnectionBlock(opened, connectionId, processId, NULL);
		if (!blockNode) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		if (opened) {
			// Store the connection opened block
			DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
			KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
			InterlockedIncrement(&blockNode->RefCount);
			if (LLRB_INSERT(BlockTree, &gConnTreeHead, blockNode)) {
				// Already stored the block
				InterlockedDecrement(&blockNode->RefCount);
			} else {
				gConnTreeCount++;
			}
			KeReleaseInStackQueuedSpinLock(&lockHandle);
			DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		}

		EnqueueBlock(blockNode);
	}

	// Release our hold on the block
	QmCleanupBlock(blockNode);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmEnqueuePacketBlock(
	__in BLOCK_NODE             *blockNode,
	__in const PACKET_DIRECTION  direction,
	__in const UINT32            capturedLength,
	__in const UINT32            packetLength,
	__in const UINT32            connectionId,
	__in const UINT16            addressFamily,
	__in const UINT8             protocol,
	__in const UINT16            port)
{
	if (!blockNode) {
		return STATUS_INVALID_PARAMETER;
	}

	if (gStatistics.NumReaders) {
		char                  *buffer;
		PCAP_NG_PACKET_HEADER *header;
		PCAP_NG_PACKET_FOOTER *footer;
		UINT32                 blockLength;
		UINT32                 blockOffset;
		UINT32                 processId;

		processId = GetProcessIdForConnectionId(connectionId, addressFamily,
				protocol, port);

		blockLength = sizeof(PCAP_NG_PACKET_HEADER) +
				PCAP_NG_PADDING(capturedLength) + sizeof(PCAP_NG_PACKET_FOOTER);
		blockNode->BlockType   = PacketBlock;
		blockNode->BlockLength = blockLength;
		blockNode->PrimaryId   = connectionId;
		GetTimestamp(&blockNode->Timestamp);
		buffer = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
		header = reinterpret_cast<PCAP_NG_PACKET_HEADER*>(buffer);
		header->BlockType      = blockNode->BlockType;
		header->BlockLength    = blockNode->BlockLength;
		header->InterfaceId    = 0;
		header->TimestampHigh  = blockNode->Timestamp.HighPart;
		header->TimestampLow   = blockNode->Timestamp.LowPart;
		header->CapturedLength = capturedLength;
		header->PacketLength   = packetLength;
		blockOffset = sizeof(PCAP_NG_PACKET_HEADER) +
				PCAP_NG_PADDING(capturedLength);
		footer = reinterpret_cast<PCAP_NG_PACKET_FOOTER*>(buffer + blockOffset);
		footer->ConnectionIdHeader.OptionCode   = 257;
		footer->ConnectionIdHeader.OptionLength = sizeof(footer->ConnectionId);
		footer->ConnectionId                    = connectionId;
		footer->ProcessIdHeader.OptionCode      = 258;
		footer->ProcessIdHeader.OptionLength    = sizeof(footer->ProcessId);
		footer->ProcessId                       = processId;
		footer->FlagsHeader.OptionCode          = 2;
		footer->FlagsHeader.OptionLength        = sizeof(footer->Flags);
		footer->Flags                           = direction;
		footer->OptionEnd.OptionCode            = 0;
		footer->OptionEnd.OptionLength          = 0;
		footer->BlockLength                     = blockNode->BlockLength;

		if (processId == _UI32_MAX) {
			HoldPacketBlock(blockNode);
		} else {
			EnqueueBlock(blockNode);
		}
	}

	// Release our hold on the block
	QmCleanupBlock(blockNode);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Since the maximum path length of 32767 characters is less than the maximum
// block option length, we do not have to worry about splitting paths or
// arguments across multiple options.  See:
// http://msdn.microsoft.com/en-us/library/aa365247.aspx
// https://github.com/HoneProject/Linux-Sensor/wiki/
//   Augmented-PCAP-Next-Generation-Dump-File-Format
__checkReturn
NTSTATUS QmEnqueueProcessBlock(
	__in const bool           started,
	__in const UINT32         pid,
	__in const UINT32         parentPid,
	__in UNICODE_STRING      *path,
	__in UNICODE_STRING      *args,
	__in UNICODE_STRING      *sid,
	__in const LARGE_INTEGER *timestamp)
{
	BLOCK_NODE         *blockNode = NULL;
	BLOCK_NODE          searchNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	// If process started, get the block node, if one already exists
	// If process ended, delete the block node, if one exists
	searchNode.PrimaryId = pid;
	if (started) {
		DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
		KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
		blockNode = LLRB_FIND(BlockTree, &gProcessTreeHead, &searchNode);
		KeReleaseInStackQueuedSpinLock(&lockHandle);
		DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		if (blockNode) {
			return STATUS_SUCCESS; // Readers already have a block for this process
		}
		InterlockedIncrement(&gStatistics.ProcessStartEvents);
		InterlockedIncrement(&gStatistics.NumProcesses);
	} else {
		DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
		KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
		blockNode = LLRB_REMOVE(BlockTree, &gProcessTreeHead, &searchNode);
		if (blockNode) {
			// In case we get multiple process close events, we only want to
			// decrement these counts one time
			gProcessTreeCount--;
			InterlockedDecrement(&gStatistics.NumProcesses);
		}
		KeReleaseInStackQueuedSpinLock(&lockHandle);
		DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		QmCleanupBlock(blockNode);
		blockNode = NULL; // So we don't free the block in the code below
		InterlockedIncrement(&gStatistics.ProcessEndEvents);
	}

	// Create a block if there are readers or need to save process information
	if (gStatistics.NumReaders || started) {
		blockNode = GetProcessBlock(started, pid, parentPid, path, args, sid,
				timestamp);
		if (!blockNode) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		// Store the process started block
		if (started) {
			DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
			KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
			InterlockedIncrement(&blockNode->RefCount);
			if (LLRB_INSERT(BlockTree, &gProcessTreeHead, blockNode)) {
				// Already stored the block
				InterlockedDecrement(&blockNode->RefCount);
			} else {
				gProcessTreeCount++;
			}
			KeReleaseInStackQueuedSpinLock(&lockHandle);
			DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
		}

		EnqueueBlock(blockNode);
	}

	// Release our hold on the block
	QmCleanupBlock(blockNode);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmGetInitialBlocks(
	__in READER_INFO *reader,
	__in const bool   useBlocksBuffer)
{
	NTSTATUS             status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE   lockHandle;
	BLOCK_NODE          *connBlock                 = NULL;
	BLOCK_NODE          *procBlock                 = NULL;
	BLOCK_NODE          *interfaceDescriptionBlock = NULL;
	BLOCK_NODE          *sectionHeaderBlock        = NULL;
	RING_BUFFER         *ringBuffer                = NULL;

	if (!reader) {
		return STATUS_INVALID_PARAMETER;
	}

	// Allocate a section header block, if there isn't one yet
	// Since the system ID in the section header block is stored in the registry,
	// we cannot get the system ID early in the boot process.  To get around
	// that, we get it here since we know that the system has finished booting
	// and that we should be at passive level.  We also neet to use interlocked
	// operations to ensure only one reader sets the actual section header block.
	// We cannot do this inside a spinlock, since that will raise the IRQL above
	// passive, which will cause a fault in the code that gets the system ID.
	sectionHeaderBlock = reinterpret_cast<BLOCK_NODE*>(
			InterlockedCompareExchangePointer(reinterpret_cast<void**>(
			&gSectionHeaderBlock), NULL, NULL));
	if (!sectionHeaderBlock && (KeGetCurrentIrql() == PASSIVE_LEVEL)) {
		BLOCK_NODE *newSectionHeaderBlock = GetSectionHeaderBlock();
		if (!newSectionHeaderBlock) {
			status = STATUS_INSUFFICIENT_RESOURCES;
		}

		// Cache the section header block for later use
		sectionHeaderBlock = reinterpret_cast<BLOCK_NODE*>(
				InterlockedCompareExchangePointer(reinterpret_cast<void**>(
				&gSectionHeaderBlock), newSectionHeaderBlock, NULL));
		if (sectionHeaderBlock) {
			// The new block wasn't used, so clean it up
			QmCleanupBlock(newSectionHeaderBlock);
		} else {
			sectionHeaderBlock = newSectionHeaderBlock;
		}
	}

	// Clean up previous initial blocks buffer if it's still allocated
	if (reader->InitialBuffer.Buffer) {
		CleanupRingBuffer(&reader->InitialBuffer);
		ExFreePool(reader->InitialBuffer.Buffer);
		reader->InitialBuffer.Buffer = NULL;
	}

	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);

	if (useBlocksBuffer) {
		ringBuffer = &reader->BlocksBuffer;
	} else {
		// Allocate new initial blocks buffer
		const UINT32 bufferSize = (gConnTreeCount + gProcessTreeCount + 2) *
				sizeof(void*);
		void **buffer = reinterpret_cast<void**>(ExAllocatePoolWithTag(
				NonPagedPool, bufferSize, gPoolTagRingBuffer));
		if (!buffer) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto Cleanup;
		}
		RtlZeroMemory(buffer, bufferSize);
		InitRingBuffer(&reader->InitialBuffer, buffer, bufferSize);
		ringBuffer = &reader->InitialBuffer;
	}

	// Enqueue section header and interface description blocks
	interfaceDescriptionBlock = GetInterfaceDescriptionBlock();
	if (!interfaceDescriptionBlock) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}
	InterlockedIncrement(&sectionHeaderBlock->RefCount);
	RingBufferEnqueue(ringBuffer, sectionHeaderBlock);
	RingBufferEnqueue(ringBuffer, interfaceDescriptionBlock);

	// Enqueue process and connection blocks by comparing timestamps
	connBlock = LLRB_MIN(BlockTree, &gConnTreeHead);
	procBlock = LLRB_MIN(BlockTree, &gProcessTreeHead);
	while (connBlock && procBlock) {
		if (procBlock->Timestamp.QuadPart < connBlock->Timestamp.QuadPart) {
			InterlockedIncrement(&procBlock->RefCount);
			RingBufferEnqueue(ringBuffer, procBlock);
			procBlock = LLRB_NEXT(BlockTree, &gProcessTreeHead, procBlock);
		} else {
			InterlockedIncrement(&connBlock->RefCount);
			RingBufferEnqueue(ringBuffer, connBlock);
			connBlock = LLRB_NEXT(BlockTree, &gConnTreeHead, connBlock);
		}
	}

	// Enqueue remaining process or connection entries
	while (procBlock) {
		InterlockedIncrement(&procBlock->RefCount);
		RingBufferEnqueue(ringBuffer, procBlock);
		procBlock = LLRB_NEXT(BlockTree, &gProcessTreeHead, procBlock);
	}
	while (connBlock) {
		InterlockedIncrement(&connBlock->RefCount);
		RingBufferEnqueue(ringBuffer, connBlock);
		connBlock = LLRB_NEXT(BlockTree, &gConnTreeHead, connBlock);
	}

Cleanup:
	// Release the spin lock here so it gets released when cleaning up
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);

	if (NT_SUCCESS(status)) {
		// Set event after releasing the spin lock
		if (reader->DataEvent) {
			KeSetEvent(reader->DataEvent, 1, FALSE);
		}
	} else if (!useBlocksBuffer && reader->InitialBuffer.Buffer) {
		CleanupRingBuffer(&reader->InitialBuffer);
		ExFreePool(reader->InitialBuffer.Buffer);
		reader->InitialBuffer.Buffer = NULL;
	}
	return status;
}

//----------------------------------------------------------------------------
UINT32 QmGetMaxSnapLen(void)
{
	return gStatistics.MaxSnapLength;
}

//----------------------------------------------------------------------------
UINT32 QmGetNumReaders(void)
{
	return gStatistics.NumReaders;
}

//----------------------------------------------------------------------------
void QmGetStatistics(__in STATISTICS *statistics, __in READER_INFO *reader)
{
	LARGE_INTEGER tickCount;

	KeQueryTickCount(&tickCount);
	memcpy(statistics, &gStatistics, sizeof(STATISTICS));
	statistics->LoadedTime       = TickDiffToSeconds(&gDriverLoadTick, &tickCount);
	statistics->LoggingTime     += TickDiffToSeconds(&gReaderTick, &tickCount);
	statistics->ReaderBufferSize = reader->RingBufferSize;
	statistics->ReaderId         = reader->Id;
	statistics->ReaderSnapLength = reader->SnapLength;

	// Both _UI32_MAX and 0 indicate unlimited snap length,
	// but we'll use 0 for consistency
	if (statistics->MaxSnapLength == _UI32_MAX) {
		statistics->MaxSnapLength = 0;
	}
	if (statistics->ReaderSnapLength == _UI32_MAX) {
		statistics->ReaderSnapLength = 0;
	}
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmRegisterReader(__in READER_INFO *reader)
{
	NTSTATUS            status = STATUS_SUCCESS;
	KLOCK_QUEUE_HANDLE  lockHandle;
	const UINT32        bufferSize = GetRingBufferSize();
	void              **buffer;

	buffer = reinterpret_cast<void**>(ExAllocatePoolWithTag(
				NonPagedPool, bufferSize, gPoolTagRingBuffer));
	if (!buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(buffer, bufferSize);

	InitRingBuffer(&reader->BlocksBuffer, buffer, bufferSize);
	status = QmGetInitialBlocks(reader, true);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	DBGPRINT(D_LOCK, "Acquiring reader list lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gReaderListLock, &lockHandle);
	InsertTailList(&gReaderListHead, &reader->ListEntry);
	if (gStatistics.NumReaders == 0) {
		KeQueryTickCount(&gReaderTick);
	}
	gStatistics.RingBufferSize = bufferSize;
	gStatistics.NumReaders++;
	gStatistics.TotalReaders++;
	reader->SnapLength     = 0;
	reader->RingBufferSize = bufferSize;
	reader->Id             = gStatistics.TotalReaders;
	DBGPRINT(D_INFO, "Registered reader %d with ring buffer size of %d, "
			"total registered readers %d", reader->Id, bufferSize,
			gStatistics.NumReaders);
	gStatistics.MaxSnapLength = _UI32_MAX; // Unlimited snap length by default
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released reader list lock at %d", __LINE__);
	return status;
}

//----------------------------------------------------------------------------
void QmSetOpenConnections(__in CONNECTIONS *connections)
{
	UINT32              index;
	KLOCK_QUEUE_HANDLE  lockHandle;

	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);
	LLRB_CLEAR(OconnTree, &gOconnTcp4TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnTcp6TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnUdp4TreeHead);
	LLRB_CLEAR(OconnTree, &gOconnUdp6TreeHead);

	for (index = 0; index < connections->NumRecords; index++) {
		OCONN_TREE_HEAD *treeHead;
		OCONN_NODE      *oconnNode = reinterpret_cast<OCONN_NODE*>(
				ExAllocateFromLookasideListEx(&gOconnNodeLal));
		if (!oconnNode) {
			break;
		}
		RtlZeroMemory(oconnNode, sizeof(OCONN_NODE));
		oconnNode->Port      = connections->Records[index].Port;
		oconnNode->ProcessId = connections->Records[index].ProcessId;
		oconnNode->Timestamp = connections->Records[index].Timestamp;

		if (connections->Records[index].AddressFamily == AF_INET) {
			if (connections->Records[index].Protocol == IPPROTO_TCP) {
				treeHead = &gOconnTcp4TreeHead;
			} else {
				treeHead = &gOconnUdp4TreeHead;
			}
		} else {
			if (connections->Records[index].Protocol == IPPROTO_TCP) {
				treeHead = &gOconnTcp6TreeHead;
			} else {
				treeHead = &gOconnUdp6TreeHead;
			}
		}

		if (LLRB_INSERT(OconnTree, treeHead, oconnNode)) {
			ExFreeToLookasideListEx(&gOconnNodeLal, oconnNode);
		}
	}

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmSetReaderDataEvent(
	__in READER_INFO  *reader,
	__in const HANDLE  userEvent)
{
	KLOCK_QUEUE_HANDLE  lockHandle;
	KEVENT             *kernelEvent = NULL;

	// Get pointer to event object
	if (userEvent != 0) {
		const NTSTATUS status = ObReferenceObjectByHandle(userEvent,
				EVENT_MODIFY_STATE, *ExEventObjectType, UserMode,
				reinterpret_cast<void**>(&kernelEvent), NULL);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	DBGPRINT(D_LOCK, "Acquiring reader list lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gReaderListLock, &lockHandle);

	// Release old object before setting the new one
	if (reader->DataEvent) {
		ObDereferenceObject(reader->DataEvent);
	}
	reader->DataEvent = kernelEvent;

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released reader list lock at %d", __LINE__);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS QmSetReaderSnapLength(
	__in READER_INFO  *reader,
	__in const UINT32  snapLength)
{
	KLOCK_QUEUE_HANDLE lockHandle;

	DBGPRINT(D_LOCK, "Acquiring reader list lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gReaderListLock, &lockHandle);
	reader->SnapLength = snapLength;
	CalculateMaxSnapLength();
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released reader list lock at %d", __LINE__);

	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
void ReleasePacketBlocks(
	__in const UINT32 connectionId,
	__in const UINT32 processId)
{
	BLOCK_NODE         *blockNode;
	BLOCK_NODE          searchNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	searchNode.PrimaryId = connectionId;
	DBGPRINT(D_LOCK, "Acquiring trees lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gTreesLock, &lockHandle);

	blockNode = LLRB_REMOVE(BlockTree, &gPacketTreeHead, &searchNode);
	if (blockNode) {
		LIST_ENTRY *head  = &blockNode->ListEntry;
		LIST_ENTRY *entry;

		do {
			// Set the process ID in the packet block and enqueue it
			char                  *buffer;
			PCAP_NG_PACKET_HEADER *header;
			PCAP_NG_PACKET_FOOTER *footer;
			UINT32                 blockOffset;
			BLOCK_NODE            *previousBlockNode;

			buffer      = blockNode->Buffer ? blockNode->Buffer : blockNode->Data;
			header      = reinterpret_cast<PCAP_NG_PACKET_HEADER*>(buffer);
			blockOffset = sizeof(PCAP_NG_PACKET_HEADER) +
					PCAP_NG_PADDING(header->CapturedLength);
			footer = reinterpret_cast<PCAP_NG_PACKET_FOOTER*>(buffer + blockOffset);
			footer->ProcessId = processId;
			EnqueueBlock(blockNode);

			// Release our hold on this block after getting the next block in the list
			DBGPRINT(D_INFO, "Releasing packet block for connection %08X",
					blockNode->PrimaryId);
			previousBlockNode = blockNode;
			entry             = blockNode->ListEntry.Flink;
			blockNode         = CONTAINING_RECORD(entry, BLOCK_NODE, ListEntry);
			QmCleanupBlock(previousBlockNode);
			gPacketTreeCount--;
		} while (entry != head);
	}

	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released trees lock at %d", __LINE__);
}

//----------------------------------------------------------------------------
UINT32 SetOption(
	__in char         *buffer,
	__in UINT32        offset,
	__in const UINT16  code,
	__in const void   *data,
	__in const UINT16  length)
{
	if (length) {
		PCAP_NG_OPTION_HEADER *option;
		option = reinterpret_cast<PCAP_NG_OPTION_HEADER*>(buffer + offset);
		option->OptionCode   = code;
		option->OptionLength = length;
		offset += sizeof(PCAP_NG_OPTION_HEADER);
		RtlCopyMemory(buffer + offset, data, option->OptionLength);
		offset += PCAP_NG_PADDING(option->OptionLength);
	}
	return offset;
}

//----------------------------------------------------------------------------
UINT32 SetUtf8Option(
	__in char                 *buffer,
	__in UINT32                offset,
	__in const UINT16          code,
	__in const UNICODE_STRING *data,
	__in UINT16                length,
	__in UINT16               *bytesRemoved)
{
	if (length) {
		ULONG                  bytesCopied;
		PCAP_NG_OPTION_HEADER *option;
		UINT16                 paddedLength;

		// Convert string to UTF-8
		option = reinterpret_cast<PCAP_NG_OPTION_HEADER*>(buffer + offset);
		offset += sizeof(PCAP_NG_OPTION_HEADER);
		RtlUnicodeToUTF8N(buffer + offset, length, &bytesCopied, data->Buffer,
				data->Length);

		// Convert command line string to argument list in-place
		if (bytesRemoved) {
			UINT16 newLength = ConvertCommandLineToArgv(buffer + offset,
					static_cast<UINT16>(bytesCopied));
			*bytesRemoved = PCAP_NG_PADDING(length) - PCAP_NG_PADDING(newLength);
			length = newLength;
		}
		option->OptionCode   = code;
		option->OptionLength = length;

		// Fill padding with nulls
		paddedLength = PCAP_NG_PADDING(option->OptionLength);
		RtlZeroMemory(buffer + offset + option->OptionLength,
				paddedLength - option->OptionLength);
		offset += paddedLength;
	}
	return offset;
}

//----------------------------------------------------------------------------
UINT32 TickDiffToSeconds(const LARGE_INTEGER *start, const LARGE_INTEGER *end)
{
	return static_cast<UINT32>(((end->QuadPart - start->QuadPart) *
			KeQueryTimeIncrement()) / 10000000);
}

#ifdef __cplusplus
};
#endif
