//----------------------------------------------------------------------------
// Hone driver entry point
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

#ifndef COMMON_H
#define COMMON_H

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------
#include <ntifs.h> // ntddk.h doesn't include all the functions that we need
#include <sal.h>

#if (_WIN32_WINNT < _WIN32_WINNT_WIN7)
#error "Only Windows 7 and later are supported"
#endif

//----------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------

#ifndef ARRAY_SIZEOF
#define ARRAY_SIZEOF(a) (sizeof(a) / sizeof(a[0]))
#endif

#endif // COMMON_H
