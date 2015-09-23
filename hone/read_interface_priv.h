//----------------------------------------------------------------------------
// Provides an interface for userspace programs to read PCAP-NG blocks
// collected by the Hone driver
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

#ifndef READ_INTERFACE_PRIV_H
#define READ_INTERFACE_PRIV_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "read_interface.h"

#include <stddef.h>
#include <wdmsec.h>

#include "queue_manager.h"
#include "hone_info.h"
#include "debug_print.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

enum RESTART_STATE {
	RestartStateNormal,   // Normal operation
	RestartStateSendEof,  // Send End-Of-File to reader
	RestartStateInit,     // Send initial PCAP-NG blocks to reader
};

struct DEVICE_EXTENSION {
	PDEVICE_OBJECT DeviceObject;
};

// Do not directly access the READER_INFO structure, since it is managed by
// the queue manager
struct READER_CONTEXT {
	DEVICE_EXTENSION      *DeviceExtension;       // Device that owns this instance
	READER_INFO            Reader;                // Reader's registration information
	RESTART_STATE          RestartState;          // Normal or restarting
	LONG                   RestartRequested;      // Non-zero if reader requested a restart
	BLOCK_NODE            *CurrentBlock;          // PCAP-NG block currently being read
	UINT32                 CurrentBlockOffset;    // Offset into current PCAP-NG block
	UINT32                *FilteredConnectionIds; // List of connection IDs being filtered (NULL if none)
	UINT32                *FilteredProcessIds;    // List of processes IDs being filtered (NULL if none)
	UINT32                 SnapLength;            // Number of bytes to capture (0 or 0xFFFFFFFF for unlimited)
	UINT32                 SnapLengthPad;         // Bytes of padding needed based on snap length
	PCAP_NG_PACKET_HEADER  ModifiedHeader;        // Modified packet header for truncated blocks
	PCAP_NG_PACKET_FOOTER  ModifiedFooter;        // Modified packet footer for truncated blocks
	UINT32                 DataEndOffset;         // Offset to end of unpadded data
	UINT32                 ModifiedFooterOffset;  // Offset to start of modified packet footer
	UINT32                 OriginalFooterOffset;  // Offset to start of original packet footer
};

struct IOCTL_PARAMS {
	UINT8  InputLength;
	UINT8  OutputLength;
	UINT8  InputLength64;
	UINT8  OutputLength64;
};

enum ID_LIST_TYPE {
	ConnectionIdList, // List of connection IDs
	ProcessIdList,    // List of process IDs
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Sets a new connection or process ID list in the reader context
/// structure
///
/// @brief context     Reader context
/// @brief idListType  Type of ID list to set
/// @brief buffer      Buffer that holds the ID list
/// @brief bufferLen   Length in bytes of buffer that holds the ID list

void SetIdList(
		READER_CONTEXT     *context,
		const ID_LIST_TYPE  idListType,
		const void         *buffer,
		const UINT32        bufferLen);

#ifdef __cplusplus
};
#endif

#endif // READ_INTERFACE_PRIV_H
