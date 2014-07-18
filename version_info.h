//----------------------------------------------------------------------------
// Hone (Host-Network) Packet-Process Correlator version information
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

#ifndef VERSION_INFO_H
#define VERSION_INFO_H

// See http://msdn.microsoft.com/en-us/library/cc194812.aspx for an example
// of using the copyright symbol

#include "version.h"
#define HONE_PRODUCTNAME_STR     "Hone (Host-Network) Packet-Process Correlator"
#define HONE_COMPANYNAME_STR     "Pacific Northwest National Laboratory"
#define HONE_LEGALCOPYRIGHT_STR  "Copyright \251 2014 Battelle Memorial Institute"

#ifdef RC_INVOKED

#ifdef VER_PRODUCTVERSION
#  undef VER_PRODUCTVERSION
#endif
#define VER_PRODUCTVERSION HONE_PRODUCTVERSION
#ifdef VER_PRODUCTVERSION_STR
#  undef VER_PRODUCTVERSION_STR
#endif
#define VER_PRODUCTVERSION_STR HONE_PRODUCTVERSION_STR
#define VER_LEGALCOPYRIGHT_STR HONE_LEGALCOPYRIGHT_STR
#ifdef VER_COMPANYNAME_STR
#  undef VER_COMPANYNAME_STR
#endif
#define VER_COMPANYNAME_STR HONE_COMPANYNAME_STR
#ifdef VER_PRODUCTNAME_STR
#  undef VER_PRODUCTNAME_STR
#endif
#define VER_PRODUCTNAME_STR HONE_PRODUCTNAME_STR

#endif // RC_INVOKED

#endif // VERSION_INFO_H
