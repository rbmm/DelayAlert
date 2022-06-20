#ifdef _M_IX86
#define _X86_
#endif

#ifdef _M_AMD64
#define _AMD64_
#endif

#include <wdm.h>

void DriverUnload(PDRIVER_OBJECT /*DriverObject*/)
{
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING )
{
	DriverObject->DriverUnload = DriverUnload;

	return STATUS_SUCCESS;
}
