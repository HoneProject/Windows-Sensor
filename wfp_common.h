//----------------------------------------------------------------------------
// Hone common Windows Filtering Platform (WFP) structures
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef WFP_COMMON_H
#define WFP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------------
// Includes
//--------------------------------------------------------------------------

#include <InitGuid.h>

//--------------------------------------------------------------------------
// Structures and enumerations
//--------------------------------------------------------------------------

// Callout types
enum CALLOUT_TYPE {
	CtConnection,
	CtPacketInbound,
	CtPacketOutbound,
};

// Filter and callout information for a WFP layer
struct LAYER_INFO {
	const char   *LayerName;     // Name of the layer
	const GUID   *LayerKey;      // Key that uniquely indentifies the layer
	const GUID   *BootFilterKey; // Key that uniquely indentifies the layer's boot-time filter
	const GUID   *FilterKey;     // Key that uniquely indentifies the layer's filter
	const GUID   *CalloutKey;    // Key that uniquely indentifies the layer's callout
	CALLOUT_TYPE  CalloutType;   // Callout type to use for this layer
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

#ifndef KERNEL
#endif

//----------------------------------------------------------------------------
/// @brief Gets information for the specified layer
///
/// @param index  Index of the layer to get information for
///
/// @returns Information for the layer if successful; NULL otherwise
const LAYER_INFO* HoneLayerInfo(const int index);

//----------------------------------------------------------------------------
/// @brief Gets the number of filtering layers
///
/// @returns Number of filtering layers
int HoneNumLayers(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WFP_COMMON_H
