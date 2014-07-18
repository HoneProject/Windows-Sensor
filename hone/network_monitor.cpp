//----------------------------------------------------------------------------
// Collects connection and packet information using Windows Filter Platform
// for the Hone driver
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

// The connection callout is an "Inline Inspection Callout", while the packet
// capture callouts are "Inline Modification Callouts"
// http://msdn.microsoft.com/en-us/library/ff570963.aspx

// TODO:
//
// Handle flows and endpoints in the connection callout:
//   http://social.msdn.microsoft.com/Forums/en/wfp/thread/6280b002-f93d-4dab-b892-1e138ceabfcd
//
// Handle IPsec processing:
//   http://msdn.microsoft.com/en-us/library/ff546423.aspx
//
// Handle more than one buffer for outbound connections:
//   http://msdn.microsoft.com/en-us/library/ff569974.aspx
//   http://social.msdn.microsoft.com/Forums/en/wfp/thread/e7ade68e-33ef-4958-97e1-4c60908bd9fa

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "network_monitor_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------

static UINT32            *gCalloutIds        = NULL;   // Registered callout IDs
static DEVICE_OBJECT     *gDevice            = NULL;   // Driver's device object
static LOOKASIDE_LIST_EX  gNetBufferLal;               // Holds memory for netbuffer storage
static bool               gNetBufferLalInit  = false;  // True if lookaside list was initialized
static LONG               gPacketCount       = 0;      // Number of packets processed
static LOOKASIDE_LIST_EX  gPacketInfoLal;              // Holds memory for packet information structures
static bool               gPacketInfoLalInit = false;  // True if lookaside list was initialized
static const UINT32       gPoolTag           = 'gNoH'; // Tag to use when allocating general pool data
static const UINT32       gPoolTagLookaside  = 'lNoH'; // Tag to use when allocating lookaside buffers

//----------------------------------------------------------------------------
UINT16 Checksum(
	void         *buffer,
	const UINT32  length,
	const UINT32  checksumIndex,
	const UINT32  innerLoopSum)
{
	UINT16  checksum;
	UINT32  sum = ChecksumInnerLoop(buffer, length, checksumIndex, innerLoopSum);

	// Take 16 bits out of the 32 bit sum and add up the carries
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	// Save the one's complement of the result in network byte order
	checksum = RtlUshortByteSwap(~sum & 0xFFFF);

	// Convert all 0 checksums to 0xFFFF, not just UDP checksums
	// http://communities.intel.com/community/wired/blog/2009/06/03/checksums-and-plus-or-minus-zero
	return (checksum == 0) ? 0xFFFF : checksum;
}

//----------------------------------------------------------------------------
UINT32 ChecksumInnerLoop(
	void   *buffer,
	UINT32  length,
	UINT32  checksumIndex,
	UINT32  innerLoopSum)
{
	UINT16 *buffer16 = reinterpret_cast<UINT16*>(buffer);
	UINT32  index;

	length        /= 2;
	checksumIndex /= 2;

	// Sum each 16 bit value in the header in host byte order
	for (index = 0; index < length; index++) {
		if (index != checksumIndex) { // Skip checksum field
			innerLoopSum += RtlUshortByteSwap(buffer16[index]);
		}
	}
	return innerLoopSum;
}

//----------------------------------------------------------------------------
void CapturePacketData(
	__in PACKET_INFO            *packetInfo,
	__in const PACKET_DIRECTION  direction)
{
	BLOCK_NODE   *blockNode        = NULL;
	char         *blockData        = NULL; // Pointer to data in the block node
	UINT32        bytesCaptured    = 0;    // Number of data bytes captured
	UINT32        bytesToCapture   = 0;    // Number of data bytes to capture
	UINT32        bytesToCopy      = 0;    // Number of bytes to copy from a buffer
	UINT32        checksumOffset   = 0;    // Offset for new checksum if generated IP header
	UINT32        dataSize         = 0;    // Packet data size in bytes
	const UINT32  maxSnapLen       = QmGetMaxSnapLen();
	UINT32        newIpHeaderSize  = 0;    // Size of generated IP header
	NET_BUFFER   *netBuffer        = NULL;
	void         *netBufferStorage = NULL; // For non-contiguous net buffer data
	LONG          packetId;                // Unique packet ID

	// Get the size of data in the net buffer list
	for (
			netBuffer  = NET_BUFFER_LIST_FIRST_NB(packetInfo->NetBufferList);
			netBuffer != NULL;
			netBuffer  = NET_BUFFER_NEXT_NB(netBuffer)) {
		dataSize += NET_BUFFER_DATA_LENGTH(netBuffer);
	}

	packetId = InterlockedIncrement(&gPacketCount);
	DBGPRINT(D_INFO, "Received %s %u byte IPv%d packet %08X on connection %08X",
			(direction == Inbound) ? "inbound" : "outbound", dataSize,
			((packetInfo->AddressFamily == AF_INET) ? 4 : 6),
			packetId, packetInfo->ConnectionId);

	// Reserve space for new IP header for outbound packets
	if ((direction == Outbound) && (packetInfo->HaveIpHeader == false)) {
		newIpHeaderSize = (packetInfo->AddressFamily == AF_INET) ?
				sizeof(IPV4_HEADER) : sizeof(IPV6_HEADER);
		dataSize += newIpHeaderSize;
	}

	if (dataSize > maxSnapLen) {
		DBGPRINT(D_WARN, "Truncating block to %u bytes", maxSnapLen);
		bytesToCapture = maxSnapLen;
	} else {
		bytesToCapture = dataSize;
	}

	// -=-=- Need to run cleanup code after this point -=-=-

	// Allocate packet buffer and maximum-sized backing storage
	blockNode = QmAllocatePacketBlock(bytesToCapture, &blockData);
	if (!blockNode) {
		DBGPRINT(D_ERR, "Cannot allocate packet block for %u bytes of data",
				bytesToCapture);
		goto Cleanup;
	}
	netBufferStorage = ExAllocateFromLookasideListEx(&gNetBufferLal);
	if (!netBufferStorage) {
		DBGPRINT(D_ERR, "Cannot allocate %u bytes for net buffer data", _UI16_MAX);
		goto Cleanup;
	}

	// Generate a new IP header for outbound packets and copy it into the block
	if (newIpHeaderSize) {
		if (packetInfo->AddressFamily == AF_INET) {
			IPV4_HEADER header = {0};

			header.VersionHeaderLen = 0x45;
			header.TotalLength      = RtlUshortByteSwap(dataSize);
			header.TimeToLive       = 128;
			header.Protocol         = packetInfo->Protocol;
			header.SrcIp            = packetInfo->SrcIp.AsUInt32;
			header.DstIp            = packetInfo->DstIp.AsUInt32;
			header.Checksum         = Checksum(&header, sizeof(header), 10);
			bytesToCopy             = (sizeof(header) > bytesToCapture) ?
					bytesToCapture : sizeof(header);
			RtlCopyMemory(blockData, &header, bytesToCopy);
			bytesCaptured += bytesToCopy;
		} else {
			IPV6_HEADER header = {0};

			header.Control       = 0x60;
			header.PayloadLength = RtlUshortByteSwap(dataSize - newIpHeaderSize);
			header.NextHeader    = packetInfo->Protocol;
			RtlCopyMemory(header.SrcIp, packetInfo->SrcIp.AsUInt8, 16);
			RtlCopyMemory(header.DstIp, packetInfo->DstIp.AsUInt8, 16);
			bytesToCopy = (sizeof(header) > bytesToCapture) ?
					bytesToCapture : sizeof(header);
			RtlCopyMemory(blockData, &header, bytesToCopy);
			bytesCaptured += bytesToCopy;
		}

		switch (packetInfo->Protocol) {
		case IPPROTO_TCP:
			checksumOffset = 16;
			break;
		case IPPROTO_UDP:
			checksumOffset = 6;
			break;
		default:
			break;
		}
	}

	// Copy packet data into the block
	for (
			netBuffer = NET_BUFFER_LIST_FIRST_NB(packetInfo->NetBufferList);
			(netBuffer != NULL) && (bytesCaptured < bytesToCapture);
			netBuffer = NET_BUFFER_NEXT_NB(netBuffer)) {
		const UINT32  netBufferSize = NET_BUFFER_DATA_LENGTH(netBuffer);
		char         *buffer;

		if (!netBufferSize) {
			continue;
		}
		bytesToCopy = (bytesCaptured + netBufferSize > bytesToCapture) ?
				bytesToCapture - bytesCaptured : netBufferSize;
		buffer = reinterpret_cast<char*>(NdisGetDataBuffer(netBuffer, bytesToCopy,
				netBufferStorage, 1, 0));
		if (!buffer) {
			DBGPRINT(D_ERR, "Cannot get %u bytes of net buffer data", bytesToCopy);
			goto Cleanup;
		}
		RtlCopyMemory(blockData + bytesCaptured, buffer, bytesToCopy);
		bytesCaptured += bytesToCopy;
	}

	// Zero padding before fixing checksums
	RtlZeroMemory(blockData + bytesCaptured,
			PCAP_NG_PADDING(bytesCaptured) - bytesCaptured);

	// Fix TCP and UDP checksums if generated IP header
	if (checksumOffset && (bytesCaptured > newIpHeaderSize + checksumOffset + 2)) {
		UINT32 innerLoopSum;
		UINT16 checksum;
		UINT32 dataLength;

		if (packetInfo->AddressFamily == AF_INET) {
			IPV4_PSEUDO_HEADER ph = {0};

			ph.SrcIp     = packetInfo->SrcIp.AsUInt32;
			ph.DstIp     = packetInfo->DstIp.AsUInt32;
			ph.Protocol  = packetInfo->Protocol;
			ph.Length    = RtlUshortByteSwap(dataSize - newIpHeaderSize);
			innerLoopSum = ChecksumInnerLoop(&ph, sizeof(ph));
		} else {
			IPV6_PSEUDO_HEADER ph = {0};

			RtlCopyMemory(ph.SrcIp, packetInfo->SrcIp.AsUInt8, 16);
			RtlCopyMemory(ph.DstIp, packetInfo->DstIp.AsUInt8, 16);
			ph.NextHeader = packetInfo->Protocol;
			ph.Length     = RtlUlongByteSwap(dataSize - newIpHeaderSize);
			innerLoopSum  = ChecksumInnerLoop(&ph, sizeof(ph));
		}

		dataLength = bytesCaptured - newIpHeaderSize;
		if (dataLength % 2) {
			dataLength++; // Data length must be a multiple of two
		}
		checksum = Checksum(blockData + newIpHeaderSize, dataLength,
				checksumOffset, innerLoopSum);
		RtlCopyMemory(blockData + newIpHeaderSize + checksumOffset, &checksum,
				sizeof(checksum));
	}

	// Enqueue block and set it to NULL so we don't free it in cleanup
	QmEnqueuePacketBlock(blockNode, direction, bytesCaptured, dataSize,
			packetInfo->ConnectionId, packetInfo->AddressFamily,
			packetInfo->Protocol, packetInfo->Port);
	blockNode  = NULL;

Cleanup:
	if (blockNode) {
		QmCleanupBlock(blockNode);
	}
	if (netBufferStorage) {
		ExFreeToLookasideListEx(&gNetBufferLal, netBufferStorage);
	}
}

//----------------------------------------------------------------------------
void ConnectionCallout(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut)
{
	bool   connectionOpened;
	UINT32 connectionId = _UI32_MAX;
	UINT32 processId    = _UI32_MAX;

	UNREFERENCED_PARAMETER(classifyContext);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);
	UNREFERENCED_PARAMETER(layerData);

	// Permit the packet to continue
	if (classifyOut && (classifyOut->rights == FWPS_RIGHT_ACTION_WRITE) &&
			(classifyOut->actionType != FWP_ACTION_BLOCK)) {
		classifyOut->actionType = FWP_ACTION_CONTINUE; // FWP_ACTION_PERMIT;
	}

	// Get event type
	switch (inFixedValues->layerId) {
	case FWPS_LAYER_ALE_AUTH_CONNECT_V4:
	case FWPS_LAYER_ALE_AUTH_CONNECT_V6:
	case FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4:
	case FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6:
	case FWPS_LAYER_ALE_RESOURCE_ASSIGNMENT_V4:
	case FWPS_LAYER_ALE_RESOURCE_ASSIGNMENT_V6:
		connectionOpened = true;
		break;
	case FWPS_LAYER_ALE_ENDPOINT_CLOSURE_V4:
	case FWPS_LAYER_ALE_ENDPOINT_CLOSURE_V6:
	case FWPS_LAYER_ALE_RESOURCE_RELEASE_V4:
	case FWPS_LAYER_ALE_RESOURCE_RELEASE_V6:
		connectionOpened = false;
		break;
	default:
		return;
	}

	// Get process ID
	if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_PROCESS_ID)) {
		const UINT64 processId64 = inMetaValues->processId;
		processId = processId64 & _UI32_MAX;
		if (processId64 > _UI32_MAX) {
			DBGPRINT(D_WARN, "Process ID %016I64X is too large", processId64);
		}
	} else {
		DBGPRINT(D_ERR, "No process ID on connection %s event",
				connectionOpened ? "open" : "close");
	}

	// Get connection ID
	if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE)) {
		const UINT64 connectionId64 = inMetaValues->transportEndpointHandle;
		connectionId = connectionId64 & _UI32_MAX;
		if (connectionId64 > _UI32_MAX) {
			DBGPRINT(D_WARN, "Connection ID %016I64X is too large", connectionId64);
		}
	} else {
		DBGPRINT(D_ERR, "No connection ID on connection %s event",
				connectionOpened ? "open" : "close");
	}

	DBGPRINT(D_INFO, "Connection %08X %s for process %u", connectionId,
			connectionOpened ? "opened" : "closed", processId);
	QmEnqueueConnectionBlock(connectionOpened, connectionId, processId);
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS DeinitializeNetworkMonitor(void)
{
	NTSTATUS status;

	if (gCalloutIds) {
		int index;
		for (index = 0; index < HoneNumLayers(); index++) {
			if (gCalloutIds[index] != 0) {
				DBGPRINT(D_INFO, "Unregistering callout for layer \"%s\"",
				HoneLayerInfo(index)->LayerName);
				status = FwpsCalloutUnregisterById(gCalloutIds[index]);
				if (!NT_SUCCESS(status)) {
					DBGPRINT(D_WARN, "Cannot unregister callout for layer \"%s\": %08X",
					HoneLayerInfo(index)->LayerName, status);
				}
			}
		}
		ExFreePool(gCalloutIds);
	}

	if (gNetBufferLalInit) {
		ExDeleteLookasideListEx(&gNetBufferLal);
	}
	if (gPacketInfoLalInit) {
		ExDeleteLookasideListEx(&gPacketInfoLal);
	}

	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS InitializeNetworkMonitor(DEVICE_OBJECT *device)
{
	NTSTATUS status;
	int      index;

	// Save pointer to the device object
	gDevice = device;

	// Allocate memory to hold registered callout IDs
	gCalloutIds = reinterpret_cast<UINT32*>(ExAllocatePoolWithTag(NonPagedPool,
			HoneNumLayers() * sizeof(UINT32), gPoolTag));
	if (!gCalloutIds) {
		DBGPRINT(D_ERR, "Cannot allocate %d callout IDs", HoneNumLayers());
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	// Register callouts (filters were already added by the user-mode utility)
	for (index = 0; index < HoneNumLayers(); index++) {
		FWPS_CALLOUT callout   = {0};
		UINT32       calloutId = 0;

		DBGPRINT(D_INFO, "Registering callout for \"%s\" layer",
				HoneLayerInfo(index)->LayerName);
		callout.calloutKey = *HoneLayerInfo(index)->CalloutKey;
		callout.notifyFn   = NotifyCallout;
		switch (HoneLayerInfo(index)->CalloutType) {
		case CtConnection:     callout.classifyFn = ConnectionCallout;     break;
		case CtPacketInbound:  callout.classifyFn = PacketCalloutInbound;  break;
		case CtPacketOutbound: callout.classifyFn = PacketCalloutOutbound; break;
		}
		status = FwpsCalloutRegister(gDevice, &callout, &calloutId);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot register callout for \"%s\" layer: %08X",
					HoneLayerInfo(index)->LayerName, status);
			gCalloutIds[index] = 0;
			return status;
		}
		gCalloutIds[index] = calloutId;
	}

	status = ExInitializeLookasideListEx(&gNetBufferLal, NULL, NULL,
			NonPagedPool, 0, _UI16_MAX, gPoolTagLookaside, 0);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create net buffer lookaside list");
		return status;
	}
	gNetBufferLalInit = true;

	status = ExInitializeLookasideListEx(&gPacketInfoLal, NULL, NULL,
			NonPagedPool, 0, _UI16_MAX, gPoolTagLookaside, 0);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create packet information lookaside list");
		return status;
	}
	gPacketInfoLalInit = true;
	return status;
}

//----------------------------------------------------------------------------
NTSTATUS NotifyCallout(
	__in FWPS_CALLOUT_NOTIFY_TYPE  notifyType,
	__in const GUID               *filterKey,
	__inout FWPS_FILTER           *filter)
{
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
void PacketCalloutInbound(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut)
{
	UINT32           headerSize    = 0;
	NET_BUFFER_LIST *netBufferList = reinterpret_cast<NET_BUFFER_LIST*>(layerData);
	PACKET_INFO      packetInfo    = {0};

	UNREFERENCED_PARAMETER(classifyContext);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);

	// Permit the packet to continue
	if (classifyOut && (classifyOut->rights == FWPS_RIGHT_ACTION_WRITE) &&
			(classifyOut->actionType != FWP_ACTION_BLOCK)) {
		classifyOut->actionType = FWP_ACTION_CONTINUE;
	}

	// Return if no readers or if the net buffer list is empty
	if (QmGetNumReaders() == 0) {
		return;
	}
	if (!netBufferList) {
		DBGPRINT(D_WARN, "No net buffer list for layer %u", inFixedValues->layerId);
		return;
	}

	// Get the connection ID, address family, port, and protocol
	packetInfo.ConnectionId = FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE) ?
			inMetaValues->transportEndpointHandle & _UI32_MAX : _UI32_MAX;
	if (inFixedValues->layerId == FWPS_LAYER_INBOUND_TRANSPORT_V4) {
		packetInfo.AddressFamily = AF_INET;
		packetInfo.Port = inFixedValues->incomingValue[
				FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_LOCAL_PORT].value.uint16;
		packetInfo.Protocol = inFixedValues->incomingValue[
				FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_PROTOCOL].value.uint8;
	} else {
		packetInfo.AddressFamily = AF_INET6;
		packetInfo.Port = inFixedValues->incomingValue[
				FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_LOCAL_PORT].value.uint16;
		packetInfo.Protocol = inFixedValues->incomingValue[
				FWPS_FIELD_INBOUND_TRANSPORT_V6_IP_PROTOCOL].value.uint8;
	}

	// Get IP and transport header sizes
	if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE)) {
		headerSize += inMetaValues->transportHeaderSize;
	}
	if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_IP_HEADER_SIZE)) {
		headerSize += inMetaValues->ipHeaderSize;
	}

	// Capture packet data for each net buffer in the list
	while (netBufferList != NULL) {
		// Retreat the buffer to get the IP header
		// http://msdn.microsoft.com/en-us/library/ff569977.aspx
		if (headerSize) {
			const NDIS_STATUS retreatStatus = NdisRetreatNetBufferDataStart(
					NET_BUFFER_LIST_FIRST_NB(netBufferList),
					headerSize, FALSE, NULL);
			if (retreatStatus != NDIS_STATUS_SUCCESS) {
				DBGPRINT(D_ERR, "Cannot retreat buffer to get headers: %08X",
						retreatStatus);
				continue;
			}
		}

		packetInfo.NetBufferList = netBufferList;
		CapturePacketData(&packetInfo, Inbound);

		// Undo the retreat
		if (headerSize) {
			NdisAdvanceNetBufferDataStart(NET_BUFFER_LIST_FIRST_NB(
					packetInfo.NetBufferList), headerSize, FALSE, NULL);
		}
		netBufferList  = NET_BUFFER_LIST_NEXT_NBL(netBufferList);
	}
}

//----------------------------------------------------------------------------
void PacketCalloutOutbound(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut)
{
	NET_BUFFER_LIST *netBufferList = reinterpret_cast<NET_BUFFER_LIST*>(layerData);
	PACKET_INFO      packetInfo    = {0};

	UNREFERENCED_PARAMETER(classifyContext);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);

	// Permit the packet to continue
	if (classifyOut && (classifyOut->rights == FWPS_RIGHT_ACTION_WRITE) &&
			(classifyOut->actionType != FWP_ACTION_BLOCK)) {
		classifyOut->actionType = FWP_ACTION_CONTINUE;
	}

	// Return if no readers or if the net buffer list is empty
	if (QmGetNumReaders() == 0) {
		return;
	}
	if (!netBufferList) {
		DBGPRINT(D_WARN, "No net buffer list for layer %u", inFixedValues->layerId);
		return;
	}

	// Get the connection ID, address family, port, and protocol
	packetInfo.ConnectionId = FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_TRANSPORT_ENDPOINT_HANDLE) ?
			inMetaValues->transportEndpointHandle & _UI32_MAX : _UI32_MAX;
	if (inFixedValues->layerId == FWPS_LAYER_OUTBOUND_TRANSPORT_V4) {
		packetInfo.AddressFamily = AF_INET;
		packetInfo.Port = inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_LOCAL_PORT].value.uint16;
		packetInfo.Protocol = inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_PROTOCOL].value.uint8;
	} else {
		packetInfo.AddressFamily = AF_INET6;
		packetInfo.Port = inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V6_IP_LOCAL_PORT].value.uint16;
		packetInfo.Protocol = inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V6_IP_PROTOCOL].value.uint8;
	}

	// Get the IP addresses (IPv6 address are already in network byte order)
	if (packetInfo.AddressFamily == AF_INET) {
		packetInfo.SrcIp.AsUInt32 = RtlUlongByteSwap(inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_LOCAL_ADDRESS].value.uint32);
		packetInfo.DstIp.AsUInt32 = RtlUlongByteSwap(inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_REMOTE_ADDRESS].value.uint32);
	} else {
		RtlCopyMemory(packetInfo.SrcIp.AsUInt8, inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V6_IP_LOCAL_ADDRESS].value.byteArray16->byteArray16, 16);
		RtlCopyMemory(packetInfo.DstIp.AsUInt8, inFixedValues->incomingValue[
				FWPS_FIELD_OUTBOUND_TRANSPORT_V6_IP_REMOTE_ADDRESS].value.byteArray16->byteArray16, 16);
	}

	// Get the existing IP header size, if any
	if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues,
			FWPS_METADATA_FIELD_IP_HEADER_SIZE)) {
		packetInfo.HaveIpHeader = inMetaValues->ipHeaderSize ? true : false;
	}

	// Capture packet data for each net buffer in the list
	while (netBufferList != NULL) {
		packetInfo.NetBufferList = netBufferList;
		CapturePacketData(&packetInfo, Outbound);
		netBufferList = NET_BUFFER_LIST_NEXT_NBL(netBufferList);
	}
}

#ifdef __cplusplus
} // extern "C"
#endif
