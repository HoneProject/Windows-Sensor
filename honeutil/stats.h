//----------------------------------------------------------------------------
// Hone user-mode utility statistics operations
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef STATS_H
#define STATS_H

//----------------------------------------------------------------------------
/// @brief Gets statistics from the Hone driver
///
/// @param verbose  Print verbose output if true
/// @param snapLen  Maximum number of bytes to capture for a packet, in bytes
///
/// @returns True if successful; false otherwise
bool GetStatistics(const bool verbose, UINT32 snapLen);

#endif // STATS_H
