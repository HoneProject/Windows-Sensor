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

//----------------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------------

#include "process_monitor_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------

// LLRB tree to hold process information
typedef LLRB_HEAD(ProcessTree, PROCESS_NODE) PROCESS_TREE_HEAD;
static PROCESS_TREE_HEAD gProcessTreeHead = LLRB_INITIALIZER(&gProcessTreeHead);

#pragma warning(push)
#pragma warning(disable:4706) // LLRB uses assignments in conditional expressions
LLRB_GENERATE(ProcessTree, PROCESS_NODE, TreeEntry, CompareProcessNodes);
#pragma warning(pop)

LLRB_CLEAR_GENERATE(ProcessTree, PROCESS_NODE, TreeEntry, DeleteProcessNode);

static UINT32            gInitializationFlags = 0;   // Components that were initialized successfully
static UINT32            gLastLoadedPid       = 0;   // ID of last process whose image was loaded
static LOOKASIDE_LIST_EX gLookasideList;             // Lookaside list for allocating LLRB nodes
static const UINT32      gPoolTag          = 'gPoH'; // Tag to use when allocating general pool data
static const UINT32      gPoolTagLookaside = 'lPoH'; // Tag to use when allocating lookaside buffers
static KSPIN_LOCK        gProcessTreeLock;           // Locks process trees

//----------------------------------------------------------------------------
// Function definition for dynamically linking ZwQueryInformationProcess()
typedef NTSTATUS (*QUERY_INFO_PROCESS)(
	__in HANDLE                                      ProcessHandle,
	__in PROCESSINFOCLASS                            ProcessInformationClass,
	__out_bcount_opt(ProcessInformationLength) PVOID ProcessInformation,
	__in UINT32                                      ProcessInformationLength,
	__out_opt PUINT32                                ReturnLength
	);
static QUERY_INFO_PROCESS ZwQueryInformationProcess;

//----------------------------------------------------------------------------
// Function definition for dynamically linking ZwQuerySystemInformation()
typedef NTSTATUS (*QUERY_SYSTEM_INFO)(
	__in UINT32       SystemInformationClass,
	__inout_opt PVOID SystemInformation,
	__in UINT32       SystemInformationLength,
	__out_opt PUINT32 ReturnLength
	);
static QUERY_SYSTEM_INFO ZwQuerySystemInformation;
static const UINT32      SystemProcessInformation         = 5;
static const UINT32      SystemExtendedProcessInformation = 57;

//----------------------------------------------------------------------------
void CleanupProcessCallback(__in HANDLE pid)
{
	PROCESS_NODE       *processNode;
	PROCESS_NODE        searchNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	// Clear the ID of last process loaded, if that process is going away
	InterlockedCompareExchange(reinterpret_cast<LONG*>(&gLastLoadedPid), 0,
			reinterpret_cast<UINT32>(pid));

	// Remove process information from process tree
	searchNode.Pid = reinterpret_cast<UINT32>(pid);
	DBGPRINT(D_LOCK, "Acquiring process tree lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gProcessTreeLock, &lockHandle);
	processNode = LLRB_REMOVE(ProcessTree, &gProcessTreeHead, &searchNode);
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released process tree lock at %d", __LINE__);
	if (processNode) {
		DBGPRINT(D_INFO, "Process %u ended: parent %u",
				reinterpret_cast<UINT32>(pid), processNode->ParentPid);
		QmEnqueueProcessBlock(false, reinterpret_cast<UINT32>(pid),
				processNode->ParentPid);
		ExFreeToLookasideListEx(&gLookasideList, processNode);
	} else {
		DBGPRINT(D_WARN, "Received cleanup notification for untracked process %u",
				pid);
	}
}

//----------------------------------------------------------------------------
int CompareProcessNodes(PROCESS_NODE *first, PROCESS_NODE *second)
{
	return (first->Pid - second->Pid);
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS CreateProcessCallback(__in HANDLE pid, __in HANDLE parentPid)
{
	// We need to wait until the process is loaded into memory to retrieve the
	// path and commandline info.  So here, we collect what we can't collect
	// there (e.g., ppid), and store it for later.
	return StoreProcessInfo(reinterpret_cast<UINT32>(pid),
			reinterpret_cast<UINT32>(parentPid), false);
}

//----------------------------------------------------------------------------
void DeleteProcessNode(PROCESS_NODE *processNode)
{
	if (processNode) {
		ExFreeToLookasideListEx(&gLookasideList, processNode);
	}
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS DeinitializeProcessMonitor(void)
{
	NTSTATUS           status;
	KLOCK_QUEUE_HANDLE lockHandle;

	if (gInitializationFlags & InitializedProcessNotifyRoutine) {
		status = PsSetCreateProcessNotifyRoutine(ProcessNotifyCallback, TRUE);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot remove callback from process notify list");
		}
	}

	if (gInitializationFlags & InitializedLoadImageNotifyRoutine) {
		status = PsRemoveLoadImageNotifyRoutine(LoadImageNotifyRoutine);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot remove callback from image notify list");
		}
	}

	DBGPRINT(D_LOCK, "Acquiring process tree lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gProcessTreeLock, &lockHandle);
	LLRB_CLEAR(ProcessTree, &gProcessTreeHead);
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released process tree lock at %d", __LINE__);

	if (gInitializationFlags & InitializedLookasideList) {
		ExDeleteLookasideListEx(&gLookasideList);
	}

	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
// To get the command line, we need to use various undocumented features.
// Here's the basic idea:
//
// By the time this callback is executed, the given process' info is loaded
// into memory, including the PEB structure and the RTL_USER_PROCESS_PARAMETERS
// structure.
//
// We use ZwQueryInformationProcess() to get the location of the process' PEB,
// and from there we can extract the RTL_USER_PROCESS_PARAMETERS structure.
// This structure is used to extract the command line.
NTSTATUS GetProcessPathArgs(
	__in const UINT32               pid,
	__in PROCESS_BASIC_INFORMATION *procBasicInfo,
	__in UNICODE_STRING            *path,
	__in UNICODE_STRING            *args)
{
	NTSTATUS                     status;
	RTL_USER_PROCESS_PARAMETERS *params;

#ifndef DBG
	// The process ID argument is only used in debugging messages
	UNREFERENCED_PARAMETER(pid);
#endif

	if (!procBasicInfo || !path || !args) {
		return STATUS_INVALID_PARAMETER;
	}

	// Get the basic process information for the attached process.  Since we're
	// attached to the process, we can use the "current process" value of -1.
	status = ZwQueryInformationProcess(ZwCurrentProcess(),
			ProcessBasicInformation, procBasicInfo,
			sizeof(PROCESS_BASIC_INFORMATION), NULL);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot get information for process %u: %08X", pid,
				status);
		return status;
	}

	// Extract path and command line information
	if (!procBasicInfo->PebBaseAddress) {
		return STATUS_INVALID_ADDRESS;
	}
	params = procBasicInfo->PebBaseAddress->ProcessParameters;
	path->Buffer = GetUnicodeStringBuffer(&params->ImagePathName, params);
	path->Length = params->ImagePathName.Length;
	args->Buffer = GetUnicodeStringBuffer(&params->CommandLine, params);
	args->Length = params->CommandLine.Length;
	return status;
}

//----------------------------------------------------------------------------
// Getting the user name itself requires SecLookupAccountSid(), which relies
// on a user-mode helper, and therefore cannot be used early in the boot
// process.  Instead, we just get the SID itself.  If we need the user name,
// a user-mode tool can use the SID to get the user name.
NTSTATUS GetProcessSid(
	__in const UINT32               pid,
	__in PROCESS_BASIC_INFORMATION *procBasicInfo,
	__in UNICODE_STRING            *sid)
{
	NTSTATUS    status;
	HANDLE      processToken     = NULL;
	TOKEN_USER *processUser      = NULL;
	ULONG       processUserBytes = 0;

#ifndef DBG
	// Since we're attached to the process, we just use the special "current
	// process" value (-1).  The process ID argument is only used in debugging
	// messages.
	UNREFERENCED_PARAMETER(pid);
#endif

	if (!procBasicInfo || !sid) {
		return STATUS_INVALID_PARAMETER;
	}

	// Open process token
	status = ZwOpenProcessTokenEx(ZwCurrentProcess(), GENERIC_READ,
			OBJ_KERNEL_HANDLE, &processToken);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot open token for process %u: %08X",
				pid, status);
		goto Cleanup;
	}

	// Get size of buffer to hold the user information, which contains the SID
	status = ZwQueryInformationToken(processToken, TokenUser,
			NULL, 0, &processUserBytes);
	if (status != STATUS_BUFFER_TOO_SMALL) {
		DBGPRINT(D_ERR, "Cannot get token information size for process %u: %08X",
				pid, status);
		goto Cleanup;
	}

	// Allocate the buffer to hold the user information
	processUser = reinterpret_cast<TOKEN_USER*>(ExAllocatePoolWithTag(
			NonPagedPool, processUserBytes, gPoolTag));
	if (processUser == NULL) {
		DBGPRINT(D_ERR, "Cannot allocate %u token information bytes for process %u",
				processUserBytes, pid);
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}

	// Get user information for the process token
	status = ZwQueryInformationToken(processToken, TokenUser,
			processUser, processUserBytes, &processUserBytes);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot get token information for process %u: %08X",
				pid, status);
		goto Cleanup;
	}

	// Convert the SID to a string, but don't free it until after enqueing the
	// PCAP-NG process block
	status = RtlConvertSidToUnicodeString(sid, processUser->User.Sid, TRUE);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot convert SID to string for process %u: %08X",
				pid, status);
		goto Cleanup;
	}

Cleanup:
	if (processToken) {
		ZwClose(processToken);
	}
	if (processUser) {
		ExFreePool(processUser);
	}
	return status;
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS GetRunningProcesses(const UINT32 infoClass, RUNNING_PROCESSES *procs)
{
	NTSTATUS status;
	UINT32   length;

	do {
		// Get buffer size.  Note that ZwQuerySystemInformation() is deprecated
		// in Windows 8, but since it seems to still work correctly and the
		// documentation does not give any viable alternatives, we still use it.
		status = ZwQuerySystemInformation(infoClass, NULL, 0, &length);
		if (status != STATUS_INFO_LENGTH_MISMATCH) {
			DBGPRINT(D_ERR, "Cannot get process information buffer length: %08X",
					status);
			return status;
		}
		if (length == 0) {
			DBGPRINT(D_ERR, "No process information");
			return STATUS_UNSUCCESSFUL;
		}
		procs->buffer = reinterpret_cast<unsigned char*>(ExAllocatePoolWithTag(
				PagedPool, length, gPoolTag));
		if (procs->buffer == NULL) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		// Get process information
		status = ZwQuerySystemInformation(infoClass, procs->buffer, length, NULL);
		if (!NT_SUCCESS(status)) {
			ExFreePool(procs->buffer);
			procs->buffer = NULL;

			// If the size of the buffer has changed between the two calls to
			// ZwQuerySystemInformation, try again.
			if (status != STATUS_INFO_LENGTH_MISMATCH) {
				DBGPRINT(D_ERR, "Cannot get process information: %08X", status);
				return status;
			}
		}
	} while (procs->buffer == NULL);

	// Since the structures used to sort the processes are smaller than the
	// system information structures, we can safely allocate a sort buffer of
	// the same size.  We could count the number of processes and allocate a
	// buffer of exactly the right size, but that would require running through
	// the system information buffer an additional time.
	procs->sorted = reinterpret_cast<PROCESS_SORT_INFO*>(
			ExAllocatePoolWithTag(PagedPool, length, gPoolTag));
	if (procs->sorted == NULL) {
		ExFreePool(procs->buffer);
		procs->buffer = NULL;
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	SortProcesses(procs);
	return STATUS_SUCCESS;
}

//----------------------------------------------------------------------------
// The RTL_USER_PROCESS_PARAMETERS struct contains two UNICODE_STRING structs
// which contain the image name and commandline arguments.  These, however,
// are not your typical UNICODE_STRING structs.
//
// On x86 machines, for _most_ processes, PUNICODE_STRING->Buffer is not a
// normal pointer to a buffer containing a unicode string, but instead is an
// _offset_ from the location of RTL_USER_PROCESS_PARAMETERS.  However, this
// does not hold for all processes.  In particular, the svchost.exe process'
// PUNICODE_STRING->Buffer variable is in fact a pointer to the location,
// _not_ an offset!
//
// On x64 machines, this is always a normal pointer.
//
// So, in the x86 case, to get the actual unicode string, we check whether
// UNICODE_STRING.Buffer is greater than the memory location of
// RTL_USER_PROCESS_PARAMETERS, and if so we calculate
// 0x20000 + RTL_USER_PROCESS_PARAMETERS->UNICODE_STRING.Buffer.
// Otherwise, we assume UNICODE_STRING.Buffer is a valid pointer, and use it
// directly.
wchar_t *GetUnicodeStringBuffer(
	__in PUNICODE_STRING              string,
	__in RTL_USER_PROCESS_PARAMETERS *processParams)
{
#ifdef _X86_
	return (reinterpret_cast<UINT32>(string->Buffer) >
			reinterpret_cast<UINT32>(processParams)) ? string->Buffer :
			reinterpret_cast<PWCH>(reinterpret_cast<UINT32>(string->Buffer) +
			reinterpret_cast<UINT32>(processParams));
#else
	UNREFERENCED_PARAMETER(processParams);
	return string->Buffer;
#endif
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS InitializeProcessMonitor(DEVICE_OBJECT *device)
{
	NTSTATUS       status;
	UNICODE_STRING routineName;

	UNREFERENCED_PARAMETER(device);

	KeInitializeSpinLock(&gProcessTreeLock);

	// Initialize lookaside list
	status = ExInitializeLookasideListEx(&gLookasideList, NULL, NULL,
			NonPagedPool, 0, sizeof(PROCESS_NODE), gPoolTagLookaside, 0);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot create lookaside list");
		return status;
	}
	gInitializationFlags |= InitializedLookasideList;

	// Load Windows functions used to grab process info
	RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");
	ZwQueryInformationProcess = reinterpret_cast<QUERY_INFO_PROCESS>
			(MmGetSystemRoutineAddress(&routineName));
	if (ZwQueryInformationProcess == NULL) {
		DBGPRINT(D_ERR, "Cannot resolve ZwQueryInformationProcess");
		return STATUS_UNSUCCESSFUL;
	}

	RtlInitUnicodeString(&routineName, L"ZwQuerySystemInformation");
	ZwQuerySystemInformation = reinterpret_cast<QUERY_SYSTEM_INFO>(
			MmGetSystemRoutineAddress(&routineName));
	if (ZwQuerySystemInformation == NULL) {
		DBGPRINT(D_ERR, "Cannot resolve ZwQuerySystemInformation");
		return STATUS_UNSUCCESSFUL;
	}

	// Register callback function for when a process gets created.
	status = PsSetCreateProcessNotifyRoutine(ProcessNotifyCallback, FALSE);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot register process create callback: %08X", status);
		return status;
	}
	gInitializationFlags |= InitializedProcessNotifyRoutine;

	// Register callback function for when an image is loaded for execution
	status = PsSetLoadImageNotifyRoutine(LoadImageNotifyRoutine);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot register image load callback: %08X", status);
		return status;
	}
	gInitializationFlags |= InitializedLoadImageNotifyRoutine;

	return QueueRunningProcesses();
}

//----------------------------------------------------------------------------
__drv_requiresIRQL(PASSIVE_LEVEL)
void LoadImageNotifyRoutine(
	__in PUNICODE_STRING fullImageName,
	__in HANDLE          pid,
	__in PIMAGE_INFO     imageInfo)
{
	PROCESS_BASIC_INFORMATION procBasicInfo;
	PROCESS_NODE             *processNode;
	PROCESS_NODE              searchNode;
	UINT32                    parentPid;
	UNICODE_STRING            path = {0};
	UNICODE_STRING            args = {0};
	UNICODE_STRING            sid  = {0};
	KLOCK_QUEUE_HANDLE        lockHandle;

	UNREFERENCED_PARAMETER(fullImageName);
	UNREFERENCED_PARAMETER(imageInfo);

	// Check if image is a driver
	if (pid == 0) {
		// TODO: Handle this.  We get the FullImageName, we just need some
		// place to store device driver info.  The trick is detecting when a
		// driver is unloaded.
		return;
	}

	// Check if this is the last process loaded
	// After a process loads, it often loads several DLLs, each of which trigger
	// this callback.  By caching the ID of the last process loaded, we can
	// avoid having to lock and search the process tree.
	if (reinterpret_cast<UINT32>(pid) == gLastLoadedPid) {
		return;
	}
	gLastLoadedPid = reinterpret_cast<UINT32>(pid);

	// Get previously stored information for the process
	// We can safely access the stored information after releasing the spin lock,
	// since the process cannot go away while we're still in the load image
	// notify routine
	searchNode.Pid = reinterpret_cast<UINT32>(pid);
	DBGPRINT(D_LOCK, "Acquiring process tree lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gProcessTreeLock, &lockHandle);
	processNode = LLRB_FIND(ProcessTree, &gProcessTreeHead, &searchNode);
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released process tree lock at %d", __LINE__);
	if (processNode) {
		if (processNode->ImageLoaded) {
			return; // The image is a DLL, which we currently ignore
		} else {
			parentPid = processNode->ParentPid;
		}
	} else {
		DBGPRINT(D_WARN, "Received image load notification for untracked process %u",
				pid);
		return;
	}
	processNode->ImageLoaded = true;

	// Get process path and arguments and process owner's SID
	GetProcessPathArgs(reinterpret_cast<UINT32>(pid), &procBasicInfo,
			&path, &args);
	GetProcessSid(reinterpret_cast<UINT32>(pid), &procBasicInfo, &sid);
	DBGPRINT(D_INFO, "Process %u starting: parent %u, path %ws", pid, parentPid,
			path.Buffer);
	QmEnqueueProcessBlock(true, reinterpret_cast<UINT32>(pid),
			parentPid, &path, &args, &sid);
	if (sid.Buffer) {
		RtlFreeUnicodeString(&sid);
	}
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS OpenHandleToProcess(__in HANDLE pid, __in PHANDLE processHandle)
{
	NTSTATUS  status;
	PEPROCESS peProcess;

	status = PsLookupProcessByProcessId(pid, &peProcess);
	if (!NT_SUCCESS(status) || !peProcess) {
		DBGPRINT(D_ERR, "Cannot lookup process %u by ID: %08X",
				reinterpret_cast<UINT32>(pid), status);
		return status;
	}
	ObDereferenceObject(peProcess);

	status = ObOpenObjectByPointer(peProcess, 0, NULL, 0, 0, KernelMode,
			processHandle);
	if (!NT_SUCCESS(status)) {
		DBGPRINT(D_ERR, "Cannot open process %u: %08X",
				reinterpret_cast<UINT32>(pid), status);
		return status;
	}
	return status;
}

//----------------------------------------------------------------------------
__drv_requiresIRQL(PASSIVE_LEVEL)
void ProcessNotifyCallback(
	__in HANDLE  parentPid,
	__in HANDLE  pid,
	__in BOOLEAN create)
{
	if (create) {
		(void)CreateProcessCallback(pid, parentPid);
	} else {
		CleanupProcessCallback(pid);
	}
}

//----------------------------------------------------------------------------
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
void QueueRunningProcess(RUNNING_PROCESSES *procs)
{
	NTSTATUS                    status;
	SYSTEM_PROCESS_INFORMATION *procInfo = procs->sorted[procs->index].Info;
	UINT32                      pid;
	UINT32                      parentPid;
	UNICODE_STRING              path = {0};
	UNICODE_STRING              args = {0};
	UNICODE_STRING              sid  = {0};

	pid       = reinterpret_cast<UINT32>(procInfo->ProcessId);
	parentPid = reinterpret_cast<UINT32>(procInfo->InheritedFromProcessId);

	// Get index of next block now so it isn't skipped by "continue"
	procs->index = procs->sorted[procs->index].Next;

	if ((pid == 0) || (pid == 4)) {
		// Fill in standard information for idle process (0) and system process (4)
		// Note that the lengths are in bytes
		if (pid) {
			path.Buffer = L"System";
			path.Length = 12;
		} else {
			path.Buffer = L"System Idle Process";
			path.Length = 38;
		}
		args.Buffer = L"";
		args.Length = 0;
		sid.Buffer  = L"S-1-5-18";
		sid.Length  = 16;

		DBGPRINT(D_INFO, "Process %u started: parent %u, path %ws", pid, parentPid,
				path.Buffer);
		QmEnqueueProcessBlock(true, pid, parentPid, &path, &args, &sid,
				&procInfo->CreateTime);
	} else {
		HANDLE                     hProcess;
		KAPC_STATE                 apcState;
		PRKPROCESS                 process;
		PROCESS_BASIC_INFORMATION  procBasicInfo;

		// Get the process' path and command line.  This is accomplished by
		// attaching to the process so we can read the PEB info, and from there
		// grab the path and command line.

		// Open a handle to the process
		status = OpenHandleToProcess(reinterpret_cast<HANDLE>(pid), &hProcess);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot open handle to process %u: %08X",
			pid, status);
		}

		// Get a reference object to the process, so we can attach this thread to
		// the process space
		status = ObReferenceObjectByHandle(hProcess, 0, *PsProcessType, KernelMode,
		reinterpret_cast<PVOID*>(&process), NULL);
		if (!NT_SUCCESS(status)) {
			DBGPRINT(D_ERR, "Cannot get reference object for process %u: %08X",
					pid, status);
			ZwClose(hProcess);
		}

		// Attach to the process' address space, get the path info, and detach
		KeStackAttachProcess(process, &apcState);
		GetProcessPathArgs(pid, &procBasicInfo, &path, &args);
		GetProcessSid(pid, &procBasicInfo, &sid);
		DBGPRINT(D_INFO, "Process %u started: parent %u, path %ws", pid, parentPid,
				path.Buffer);
		QmEnqueueProcessBlock(true, pid, parentPid, &path, &args, &sid,
				&procInfo->CreateTime);
		KeUnstackDetachProcess(&apcState);
		ObDereferenceObject(process);
		ZwClose(hProcess);
		if (sid.Buffer) {
			RtlFreeUnicodeString(&sid);
		}
	}

	// Store the process information
	StoreProcessInfo(pid, parentPid, true);
}

//----------------------------------------------------------------------------
// If a process arrives while this code is running, the process creation
// callback should handle it.
__checkReturn __drv_requiresIRQL(PASSIVE_LEVEL)
NTSTATUS QueueRunningProcesses(void)
{
	NTSTATUS          status;
	RUNNING_PROCESSES procs    = {0};
	RUNNING_PROCESSES extProcs = {0};

	// Use two different information classes to get the running processes
	// so we're a bit more resistant to malware.  We could also check
	// SystemSessionProcessInformation, but we would need a way to get
	// the session IDs in the driver.  See http://wj32.org/wp/2009/04/25
	status = GetRunningProcesses(SystemProcessInformation, &procs);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}
	status = GetRunningProcesses(SystemExtendedProcessInformation, &extProcs);
	if (!NT_SUCCESS(status)) {
		goto Cleanup;
	}

	// Queue up the processes from oldest to newest
	while ((procs.index != -1) && (extProcs.index != -1)) {
		const UINT32 pid = reinterpret_cast<UINT32>(
				procs.sorted[procs.index].Info->ProcessId);
		const UINT32 extPid = reinterpret_cast<UINT32>(
				extProcs.sorted[extProcs.index].Info->ProcessId);

		if (pid <= extPid) {
			QueueRunningProcess(&procs);
			if (pid == extPid) {
				// Skip extended process since PIDs are equal
				extProcs.index = extProcs.sorted[extProcs.index].Next;
			}
		} else {
			QueueRunningProcess(&extProcs);
		}
	}
	while (procs.index != -1) {
		QueueRunningProcess(&procs);
	}
	while (extProcs.index != -1) {
		QueueRunningProcess(&extProcs);
	}

Cleanup:
	if (procs.buffer) {
		ExFreePool(procs.buffer);
	}
	if (procs.sorted) {
		ExFreePool(procs.sorted);
	}
	if (extProcs.buffer) {
		ExFreePool(extProcs.buffer);
	}
	if (extProcs.sorted) {
		ExFreePool(extProcs.sorted);
	}
	return status;
}

//----------------------------------------------------------------------------
// Processes are sorted by PID, so they are not in timestamp order if the PID
// has rolled over.  However, they will be in timestamp order if the PID has
// not rolled over, which is often the case.  Because of this, we sort the
// processes in reverse order (newest to oldest), which is more efficient.
void SortProcesses(RUNNING_PROCESSES *procs)
{
	SYSTEM_PROCESS_INFORMATION *procInfo = NULL;
	INT32                       tail     = 0; // Tail of the sort buffer
	INT32                       index    = 0; // Index into sort buffer
	INT32                       offset   = 0; // Offset into process buffer

	procs->index = 0;
	do {
		INT32 next = -1;   // Index of next block in linked list
		INT32 curr = tail; // Index of first block in linked list

		// Insert block for this process
		procInfo = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(
				procs->buffer + offset);
		procs->sorted[index].Prev      = -1;
		procs->sorted[index].Next      = -1;
		procs->sorted[index].Timestamp = procInfo->CreateTime.QuadPart;
		procs->sorted[index].Info      = procInfo;

		// Find the last block with a timestamp less than this block and insert
		// this block after it
		while (curr != -1) {
			if (procs->sorted[curr].Timestamp < procs->sorted[index].Timestamp) {
				next = procs->sorted[curr].Next;
				procs->sorted[index].Next = next;
				procs->sorted[index].Prev = curr;
				procs->sorted[curr].Next  = index;
				if (next != -1) {
					procs->sorted[next].Prev = index;
				}
				break;
			}

			// Move to previous entry in the list
			next = curr;
			curr = procs->sorted[curr].Prev;
		}

		if (next == -1) {
			// This block is at the tail of list
			tail = index;
		} else if ((curr == -1) && (next != index)) {
			// This block is at the head of list
			procs->index = index;
			procs->sorted[index].Next = next;
			procs->sorted[next].Prev  = index;
		}

		index++;
		offset += procInfo->NextEntryOffset;
	} while (procInfo->NextEntryOffset != 0);
}

//----------------------------------------------------------------------------
__checkReturn
NTSTATUS StoreProcessInfo(
	__in const UINT32 pid,
	__in const UINT32 parentPid,
	__in const bool   imageLoaded)
{
	PROCESS_NODE       *processNode;
	PROCESS_NODE       *insertNode;
	KLOCK_QUEUE_HANDLE  lockHandle;

	processNode = reinterpret_cast<PROCESS_NODE*>(ExAllocateFromLookasideListEx(
			&gLookasideList));
	if (processNode == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	processNode->Pid         = pid;
	processNode->ParentPid   = parentPid;
	processNode->ImageLoaded = imageLoaded;
	DBGPRINT(D_LOCK, "Acquiring process tree lock at %d", __LINE__);
	KeAcquireInStackQueuedSpinLock(&gProcessTreeLock, &lockHandle);
	insertNode = LLRB_INSERT(ProcessTree, &gProcessTreeHead, processNode);
	KeReleaseInStackQueuedSpinLock(&lockHandle);
	DBGPRINT(D_LOCK, "Released process tree lock at %d", __LINE__);
	if (insertNode) {
		DBGPRINT(D_WARN, "Already storing information for process %u", pid);
		ExFreeToLookasideListEx(&gLookasideList, processNode);
	}
	return STATUS_SUCCESS;
}

#ifdef __cplusplus
}; // extern "C"
#endif
