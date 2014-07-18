//----------------------------------------------------------------------------
// Hone user-mode utility common functions
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef COMMON_H
#define COMMON_H

//--------------------------------------------------------------------------
/// @brief Prints the specified error code's description
///
/// @param errorCode  Error code to use
void LogError(const UINT32 errorCode);

//--------------------------------------------------------------------------
/// @brief Prints the message followed by the last error code's description
///
/// @param format  Message format
/// @param ...     Additional message arguments
void LogError(const char *format, ...);

//--------------------------------------------------------------------------
/// @brief Prints the message followed by the specified error code's description
///
/// @param errorCode  Error code to use
/// @param format     Message format
/// @param ...        Additional message arguments
void LogError(const UINT32 errorCode, const char *format, ...);

//----------------------------------------------------------------------------
/// @brief Open the Hone driver
///
/// @param verbose  Print verbose output if true
///
/// @returns 0 if successful; error code otherwise
HANDLE OpenDriver(const bool verbose);

#endif // COMMON_H
