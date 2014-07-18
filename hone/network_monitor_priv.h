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

#ifndef NETWORK_MONITOR_PRIV_H
#define NETWORK_MONITOR_PRIV_H

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "network_monitor.h"

// Includes that produce "unnamed struct/union" warnings
#pragma warning(push)
#pragma warning(disable:4201)
#include <ndis.h>
#include <fwpsk.h>
#pragma warning(pop)

// Other system includes
#define INITGUID
#include <fwpmk.h>
#include <guiddef.h>
#include <limits.h>
#include <wdf.h>

// Project includes
#include "../wfp_common.h"
#include "queue_manager.h"
#include "hone_info.h"
#include "debug_print.h"

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

union IP_ADDRESS {
	UINT8   AsUInt8[16];  // IPv6
	UINT32  AsUInt32;     // IPv4
};

// Basic IPv4 header
struct IPV4_HEADER {
	UINT8  VersionHeaderLen;
	UINT8  TypeOfService;
	UINT16 TotalLength;
	UINT16 Identification;
	UINT16 FragmentOffset;
	UINT8  TimeToLive;
	UINT8  Protocol;
	UINT16 Checksum;
	UINT32 SrcIp;
	UINT32 DstIp;
};

// Basic IPv6 header
struct IPV6_HEADER {
	UINT32 Control;        // Version, traffic class, and flow label
	UINT16 PayloadLength;
	UINT8  NextHeader;
	UINT8  HopLimit;
	UINT8  SrcIp[16];
	UINT8  DstIp[16];
};

// IPv4 pseudo-header for TCP and UDP checksum calculations
struct IPV4_PSEUDO_HEADER {
	UINT32 SrcIp;
	UINT32 DstIp;
	UINT8  Zero;
	UINT8  Protocol;
	UINT16 Length;
};

// IPv6 pseudo-header for TCP and UDP checksum calculations
struct IPV6_PSEUDO_HEADER {
	UINT8  SrcIp[16];
	UINT8  DstIp[16];
	UINT32 Length;
	UINT8  Zero[3];
	UINT8  NextHeader;
};

// Information needed to capture packet data
struct PACKET_INFO {
	ADDRESS_FAMILY   AddressFamily;  // IPv4 or IPv6
	UINT32           ConnectionId;   // 32-bit connection associated with packet
	bool             HaveIpHeader;   // True if outbound packet has an IP header
	NET_BUFFER_LIST *NetBufferList;  // Holds packet data
	UINT16           Port;           // Local port associated with packet
	UINT8            Protocol;       // IP protocol for this packet
	IP_ADDRESS       SrcIp;          // Source IP address for outbound packets
	IP_ADDRESS       DstIp;          // Destination IP address for outbound packets
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Calculates the checksum for a buffer
///
/// See RFC 1701 - https://tools.ietf.org/html/rfc1071
///
/// @param buffer         Buffer to calculate the checksum for
/// @param length         Buffer length in bytes
/// @param checksumIndex  Offset to checksum field in the buffer (_UI32_MAX if none)
/// @param innerLoopSum   Inner loop sum value to carry forward
///
/// @returns The checksum
UINT16 Checksum(
	void         *buffer,
	const UINT32  length,
	const UINT32  checksumIndex = _UI32_MAX,
	const UINT32  innerLoopSum  = 0);

//----------------------------------------------------------------------------
/// @brief Sums the buffer contents, 16 bits at a time
///
/// @param buffer         Buffer to calculate the checksum for
/// @param length         Buffer length in bytes
/// @param checksumIndex  Offset to checksum field in the buffer (_UI32_MAX if none)
/// @param checksumSeed   Seed checksum value to carry forward
///
/// @returns The inner loop result
UINT32 ChecksumInnerLoop(
	void   *buffer,
	UINT32  length,
	UINT32  checksumIndex = _UI32_MAX,
	UINT32  innerLoopSum  = 0);

//----------------------------------------------------------------------------
/// @brief Captures and enqueues data from the packet
///
/// @param packetInfo  Information about the packet to capture data for
/// @param direction   Inbound or outbound
void CapturePacketData(
	__in PACKET_INFO            *packetInfo,
	__in const PACKET_DIRECTION  direction);

//----------------------------------------------------------------------------
/// @brief Processes connection open and close information
///
/// @param inFixedValues  Values for each data field at the layer being filtered
/// @param inMetaValues   Values for each metadata field at the layer being filtered
/// @param layerData      Raw data being filtered
/// @param filter         Information for filter associated with the callout
/// @param flowContext    Context associated with the data flow (0 if no context)
/// @param classifyOut    Data returned to the caller
void ConnectionCallout(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut);

//----------------------------------------------------------------------------
/// @brief Called when a filter is added to or deleted from the engine
///
/// @param notifyType  Type of notification (add or delete)
/// @param filterKey   Key associated with the filter
/// @param filter      Information for filter being added or deleted
///
/// @returns STATUS_SUCCESS
NTSTATUS NotifyCallout(
	__in FWPS_CALLOUT_NOTIFY_TYPE  notifyType,
	__in const GUID               *filterKey,
	__inout FWPS_FILTER           *filter);

//----------------------------------------------------------------------------
/// @brief Captures data from inbound packets
///
/// @param inFixedValues  Values for each data field at the layer being filtered
/// @param inMetaValues   Values for each metadata field at the layer being filtered
/// @param layerData      Raw data being filtered
/// @param filter         Information for filter associated with the callout
/// @param flowContext    Context associated with the data flow (0 if no context)
/// @param classifyOut    Data returned to the caller
void PacketCalloutInbound(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut);

//----------------------------------------------------------------------------
/// @brief Captures data from outbound packets
///
/// @param inFixedValues  Values for each data field at the layer being filtered
/// @param inMetaValues   Values for each metadata field at the layer being filtered
/// @param layerData      Raw data being filtered
/// @param filter         Information for filter associated with the callout
/// @param flowContext    Context associated with the data flow (0 if no context)
/// @param classifyOut    Data returned to the caller
void PacketCalloutOutbound(
	__in const FWPS_INCOMING_VALUES          *inFixedValues,
	__in const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
	__inout_opt void                         *layerData,
	__in_opt const void                      *classifyContext,
	__in const FWPS_FILTER                   *filter,
	__in UINT64                               flowContext,
	__out FWPS_CLASSIFY_OUT                  *classifyOut);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NETWORK_MONITOR_PRIV_H
