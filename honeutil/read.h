//----------------------------------------------------------------------------
// Hone user-mode utility read operations
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef READ_H
#define READ_H

//----------------------------------------------------------------------------
/// @brief Reads PCAP-NG blocks from the Hone driver
///
/// @param verbose  Print verbose output if true
/// @param logDir   Directory to save log files in
/// @param snapLen  Maximum number of bytes to capture for a packet, in bytes
///
/// @returns True if successful; false otherwise
bool ReadDriver(const bool verbose, const char *logDir, UINT32 snapLen);

#endif // READ_H
