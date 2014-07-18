//----------------------------------------------------------------------------
// Collects process information for the Hone driver
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

#ifndef PROCESS_MONITOR_PRIV_H
#define PROCESS_MONITOR_PRIV_H

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "process_monitor.h"

#include "queue_manager.h"
#include "hone_info.h"
#include "debug_print.h"
#include "llrb_clear.h"

//----------------------------------------------------------------------------
// Structures and enumerations
//----------------------------------------------------------------------------

// Structures needed to get command line info from a process
// Simplified versions of those found in winternl.h
struct RTL_USER_PROCESS_PARAMETERS {
	unsigned char   Reserved1[16];
	void           *Reserved2[10];
	UNICODE_STRING  ImagePathName;
	UNICODE_STRING  CommandLine;
};
typedef struct _PEB {
	void                        *Reserved[4];
	RTL_USER_PROCESS_PARAMETERS *ProcessParameters;
} PEB, *PPEB;

// Simplified system process information structure
struct SYSTEM_PROCESS_INFORMATION {
	UINT32         NextEntryOffset;
	UINT32         NumberOfThreads;
	LARGE_INTEGER  Reserved[3];
	LARGE_INTEGER  CreateTime;
	LARGE_INTEGER  UserTime;
	LARGE_INTEGER  KernelTime;
	UNICODE_STRING ImageName;
	KPRIORITY      BasePriority;
	HANDLE         ProcessId;
	HANDLE         InheritedFromProcessId;
	// ...
};

// Information for sorting processes by timestamp
struct PROCESS_SORT_INFO {
	INT32                       Prev;      // Index of previous block
	INT32                       Next;      // Index of next block
	UINT64                      Timestamp; // Timestamp for this block
	SYSTEM_PROCESS_INFORMATION *Info;
};

// Information for running processes
struct RUNNING_PROCESSES {
	unsigned char     *buffer;  // Buffer to hold information for the processes
	PROCESS_SORT_INFO *sorted;  // Processes sorted by timestamp
	INT32              index;   // Index of current process in sorted buffer
};

// An LLRB tree node that holds process information
typedef bool _Bool;
struct PROCESS_NODE {
	LLRB_ENTRY(PROCESS_NODE) TreeEntry;    // LLRB tree entry
	UINT32                   Pid;          // Process ID
	UINT32                   ParentPid;    // Parent process ID
	bool                     ImageLoaded;  // True if process image loaded in memory
};

// Flags to track components that were successfully initialized
enum INIT_FLAGS {
	InitializedLookasideList          = 0x0001,
	InitializedProcessNotifyRoutine   = 0x0002,
	InitializedLoadImageNotifyRoutine = 0x0004,
};

//----------------------------------------------------------------------------
// Function prototypes
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
/// @brief Called when a process is being cleaned up
///
/// @param pid  ID of the process being cleaned up
void CleanupProcessCallback(__in HANDLE pid);

//----------------------------------------------------------------------------
/// @brief Compare two process nodes for sorting the LLRB tree
///
/// @param first   First process node to compare
/// @param second  Second process node to compare
///
/// @returns <0 if first node's process ID is less than second;
///           0 if nodes' process IDs are equal
///          >0 if first node's process ID is greater than second
int CompareProcessNodes(PROCESS_NODE *first, PROCESS_NODE *second);

//----------------------------------------------------------------------------
/// @brief Called when a new process is being created
///
/// The PEB isn't fully constructed yet, so we can't access it to retrieve
/// any information.  Instead, we stash any info we can't collect later.
///
/// @param pid        ID of the process being created
/// @param parentPid  ID of the parent of the process
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS CreateProcessCallback(__in HANDLE pid, __in HANDLE parentPid);

//----------------------------------------------------------------------------
/// @brief Deletes a process node in the LLRB tree
///
/// @param processNode  Pointer to node to delete
void DeleteProcessNode(PROCESS_NODE *processNode);

//----------------------------------------------------------------------------
/// @brief Gets path and argument string for a process
///
/// @param pid            ID of the process to get information for
/// @param procBasicInfo  Basic information structure for the process
/// @param path           Structure to hold process path
/// @param args           Structure to hold process argument string
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
NTSTATUS GetProcessPathArgs(
	__in const UINT32               pid,
	__in PROCESS_BASIC_INFORMATION *procBasicInfo,
	__in UNICODE_STRING            *path,
	__in UNICODE_STRING            *args);

//----------------------------------------------------------------------------
/// @brief Gets SID for a process
///
/// The caller must free the SID string in the process strings structure
///
/// @param pid            ID of the process to get information for
/// @param procBasicInfo  Basic information structure for the process
/// @param sid            Structure to hold process owner's security ID string
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
NTSTATUS GetProcessSid(
	__in const UINT32               pid,
	__in PROCESS_BASIC_INFORMATION *procBasicInfo,
	__in UNICODE_STRING            *sid);

//----------------------------------------------------------------------------
/// @brief Gets information about running processes sorted by timestamp
///
/// Valid information classes are:
///  * SystemProcessInformation
///  * SystemExtendedProcessInformation
/// Caller is responsible for freeing buffer memory using ExFreePool.
///
/// @param infoClass  Information class to use
/// @param procs      Holds information for running processes
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS GetRunningProcesses(const UINT32 infoClass, RUNNING_PROCESSES *procs);

//----------------------------------------------------------------------------
/// @brief Gets the pointer to the buffer of a Unicode string found within the
/// process parameters structure
///
/// @param string         String to get buffer for
/// @param processParams  Process parameters that contain the string
///
/// @returns Pointer to the Unicode string buffer
wchar_t *GetUnicodeStringBuffer(
	__in PUNICODE_STRING              string,
	__in RTL_USER_PROCESS_PARAMETERS *processParams);

//----------------------------------------------------------------------------
/// @brief Called whenever an executable image is mapped into virtual memory
///
/// This function is called for all EXEs and DLLs.  For now we only care about
/// EXEs, so we ignore DLLs.
///
/// Runs in the context of the loaded process.
///
/// @param fullImageName  Full path to loaded image
/// @param pid            ID of the process that loaded the image
/// @param imageInfo      Information about the loaded image
__drv_requiresIRQL(PASSIVE_LEVEL)
void LoadImageNotifyRoutine(
	__in PUNICODE_STRING fullImageName,
	__in HANDLE          pid,
	__in PIMAGE_INFO     imageInfo);

//----------------------------------------------------------------------------
/// @brief Opens a handle to the process
///
/// @param pid            ID of the process to open handle to
/// @param processHandle  Newly opened handle to the process
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS OpenHandleToProcess(__in HANDLE pid, __in PHANDLE processHandle);

//----------------------------------------------------------------------------
/// @brief Called when a new process is created or deleted
///
/// @param parentPid  ID of the parent of the process
/// @param pid        ID of the process that was created or deleted
/// @param create     True if process was created, false if deleted
__drv_requiresIRQL(PASSIVE_LEVEL)
void ProcessNotifyCallback(
	__in HANDLE  parentPid,
	__in HANDLE  pid,
	__in BOOLEAN create);

//----------------------------------------------------------------------------
/// @brief Queues a single process currently running on the machine
///
/// Advances the process index to the index of the next block, and sets it to
/// -1 if there are no other blocks
///
/// @param procs  Information for the processes
__drv_requiresIRQL(PASSIVE_LEVEL)
void QueueRunningProcess(RUNNING_PROCESSES *procs);

//----------------------------------------------------------------------------
/// @brief Queues up all processes currently running on the machine
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS QueueRunningProcesses(void);

//----------------------------------------------------------------------------
/// @brief Sorts processes by timestamp
///
/// To get the sorted blocks, start at the head block and use each block's
/// next index to get the next block until the index is -1.
///
/// @param procs  Information for the processes
void SortProcesses(RUNNING_PROCESSES *procs);

//----------------------------------------------------------------------------
/// @brief Store process information for later retrieval
///
/// @param pid          ID of the process to store
/// @param parentPid    ID of the parent of the process
/// @param imageLoaded  True if process image has already been loaded
///
/// @returns STATUS_SUCCESS if successful; NTSTATUS error code otherwise
__checkReturn
NTSTATUS StoreProcessInfo(
	__in const UINT32 pid,
	__in const UINT32 parentPid,
	__in const bool   imageLoaded);

#ifdef __cplusplus
}; // extern "C"
#endif

#endif	// PROCESS_MONITOR_PRIV_H
