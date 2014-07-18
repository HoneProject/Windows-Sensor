//----------------------------------------------------------------------------
// Hone user-mode utility filter operations
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef FILTERS_H
#define FILTERS_H

//----------------------------------------------------------------------------
/// @brief Installs or uninstalls the WFP filters used by the Hone driver
///
/// @param verbose  Print verbose output if true
/// @param install  Install the filters if true and uninstall if false
///
/// @returns True if successful; false otherwise
bool SetupFilters(const bool verbose, const bool install);

#endif // FILTERS_H
