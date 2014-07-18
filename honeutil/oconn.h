//----------------------------------------------------------------------------
// Hone user-mode utility open connection operations
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef OCONN_H
#define OCONN_H

//----------------------------------------------------------------------------
/// @brief Sends list of open connections to the Hone driver
///
/// @param verbose  Print verbose output if true
///
/// @returns True if successful; false otherwise
bool SendOptionConnections(const bool verbose);

#endif // OCONN_H
