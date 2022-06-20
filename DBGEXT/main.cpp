#include "stdafx.h"

_NT_BEGIN

typedef struct KLDR_DATA_TABLE_ENTRY {
	LIST_ENTRY InLoadOrderLinks;
	PVOID ExceptionTable;
	ULONG ExceptionTableSize;
	// ULONG padding on IA64
	PVOID GpValue;
	PNON_PAGED_DEBUG_INFO NonPagedDebugInfo;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
} *PKLDR_DATA_TABLE_ENTRY;

HRESULT CALLBACK DebugExtensionInitialize(PULONG Version, PULONG Flags) 
{
	*Version = DEBUG_EXTENSION_VERSION(1, 0);
	*Flags = 0; 
	return S_OK;
}

void CALLBACK DebugExtensionUninitialize()
{
}

HRESULT CALLBACK DebugExtensionCanUnload()
{
	return S_OK;
}

void CALLBACK DebugExtensionUnload()
{
}

HRESULT GetExportFunc_I(ULONG64 DllBase, IDebugDataSpaces* pDataSpace, ULONG Ordinal, _Out_ PULONG_PTR pfunc)
{
	union {
		IMAGE_DOS_HEADER idh;
		IMAGE_NT_HEADERS32 inth32;
		IMAGE_NT_HEADERS64 inth64;
	};

	HRESULT hr = HRESULT_FROM_NT(STATUS_INVALID_IMAGE_FORMAT);

	ULONG cb;
	if (
		0 <= pDataSpace->ReadVirtual(DllBase, &idh, sizeof(idh), &cb) && 
		cb == sizeof(idh) &&
		idh.e_magic == IMAGE_DOS_SIGNATURE &&
		0 <= pDataSpace->ReadVirtual(DllBase + idh.e_lfanew, &inth64, sizeof(inth64), &cb) && 
		cb == sizeof(inth64) &&
		inth64.Signature == IMAGE_NT_SIGNATURE
		)
	{
		PIMAGE_DATA_DIRECTORY DataDirectory;

		switch (inth64.OptionalHeader.Magic)
		{
		case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
			DataDirectory = &inth32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
			break;
		case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
			DataDirectory = &inth64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
			break;
		default: return hr;
		}

		ULONG Size = DataDirectory->Size;

		if (Size - sizeof(IMAGE_EXPORT_DIRECTORY) < MAXUSHORT)
		{
			union {
				PVOID buf;
				PIMAGE_EXPORT_DIRECTORY pied;
			};

			if (buf = LocalAlloc(0, cb))
			{
				ULONG64 VirtualAddress = DllBase + DataDirectory->VirtualAddress;

				if (0 <= pDataSpace->ReadVirtual(VirtualAddress, pied, Size, &cb) && Size == cb)
				{
					hr = HRESULT_FROM_NT(STATUS_ORDINAL_NOT_FOUND);

					if (ULONG NumberOfFunctions = pied->NumberOfFunctions)
					{
						if ((Ordinal -= pied->Base) < NumberOfFunctions)
						{
							ULONG rva;

							ULONG64 pRva = DllBase + pied->AddressOfFunctions + Ordinal * sizeof(rva) - VirtualAddress;

							if (pRva < Size && pRva + sizeof(ULONG) < Size)
							{
								ULONG_PTR func = DllBase + *(ULONG*)RtlOffsetToPointer(pied, pRva);

								if (func - VirtualAddress >= Size)
								{
									*pfunc = func;
									hr = S_OK;
								}
							}
						}
					}
				}

				LocalFree(buf);
			}
		}
	}

	return hr;
}

PLIST_ENTRY ReadBlink(PLIST_ENTRY Entry, IDebugDataSpaces* pDataSpace)
{
	ULONG cb;
	PLIST_ENTRY Blink;
	return 0 <= pDataSpace->ReadVirtual((ULONG64)(ULONG_PTR)&Entry->Blink, &Blink, sizeof(Blink), &cb) && 
		cb == sizeof(Blink) ? Blink : 0;
}

HRESULT GetExportFunc(PLIST_ENTRY PsLoadedModuleList, IDebugDataSpaces* pDataSpace, ULONG Ordinal, _Out_ PULONG_PTR pfunc)
{
	ULONG n = 0x100;

	PLIST_ENTRY Entry = PsLoadedModuleList;

	while (--n && (Entry = ReadBlink(Entry, pDataSpace)) && (Entry != PsLoadedModuleList))
	{
		KLDR_DATA_TABLE_ENTRY DataTableEntry;

		ULONG cb;
		if (0 > pDataSpace->ReadVirtual((ULONG64)(ULONG_PTR)CONTAINING_RECORD(Entry, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks),
			&DataTableEntry, sizeof(DataTableEntry), &cb) || cb != sizeof(DataTableEntry))
		{
			break;
		}

		STATIC_UNICODE_STRING(Flexx, "Flexx.dll");

		WCHAR buf[0x20];

		if (DataTableEntry.BaseDllName.Length == Flexx.Length)
		{
			if (0 <= pDataSpace->ReadVirtual((ULONG64)(ULONG_PTR)DataTableEntry.BaseDllName.Buffer, 
				buf, Flexx.Length, &cb) && cb == Flexx.Length)
			{
				DataTableEntry.BaseDllName.Buffer = buf;

				if (RtlEqualUnicodeString(&DataTableEntry.BaseDllName, &Flexx, TRUE))
				{
					return GetExportFunc_I((ULONG64)(ULONG_PTR)DataTableEntry.DllBase, pDataSpace, Ordinal, pfunc);
				}
			}
		}
	}

	return HRESULT_FROM_NT(STATUS_DLL_NOT_FOUND);
}

struct STACK
{
	union {
		LONGLONG Interval;
		ULONG_PTR Thread;
	};
	ULONG_PTR Rip;
	ULONG_PTR z[9];
};

void common(IDebugClient* pDebugClient, STACK* stack, ULONG Ordinal, PCSTR msg)
{
	IDebugControl* pDebugControl;
	IDebugDataSpaces* pDataSpace;
	IDebugAdvanced* pDebugAdv;
	IDebugSystemObjects* pDebugSysObj;

	//if (IsDebuggerPresent()) __debugbreak();

	if (0 <= pDebugClient->QueryInterface(IID_PPV_ARGS(&pDebugControl)))
	{
		pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "%s(%p)\n", msg, stack->Thread);
		msg = 0;

		HRESULT hr;

		if (stack->Interval == MAXLONG_PTR)
		{
			hr = HRESULT_FROM_NT(STATUS_INVALID_PARAMETER_1);
			goto __exit;
		}

		ULONG Class, Qualifier;

		if (0 > (hr = pDebugControl->GetDebuggeeType(&Class, &Qualifier)))
		{
			msg = "!! GetDebuggeeType\n";
			goto __exit;
		}

		if (DEBUG_CLASS_KERNEL != Class)
		{
			pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "DebuggeeType=%x.%x - not KERNEL !\n", Class, Qualifier);
			hr = HRESULT_FROM_NT(RPC_NT_UNSUPPORTED_TYPE);
			goto __exit;
		}

		if (0 <= (hr = pDebugClient->QueryInterface(IID_PPV_ARGS(&pDataSpace))))
		{
			PLIST_ENTRY PsLoadedModuleList;

			if (0 <= (hr = pDataSpace->ReadDebuggerData(DEBUG_DATA_PsLoadedModuleListAddr, 
				&PsLoadedModuleList, sizeof(PsLoadedModuleList), 0)))
			{
				if (PsLoadedModuleList)
				{
					ULONG_PTR func = 0;

					hr = GetExportFunc(PsLoadedModuleList, pDataSpace, Ordinal, &func);

					pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "func = %p\n", func);

					if (0 <= hr && 0 <= (hr = pDebugClient->QueryInterface(IID_PPV_ARGS(&pDebugSysObj))))
					{
						ULONG64 Thread;

						if (0 <= pDebugSysObj->GetCurrentThreadDataOffset(&Thread))
						{
							pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "KTHREAD = %p\n", Thread);
						}
						
						pDebugSysObj->Release();

						if (0 <= (hr = pDebugClient->QueryInterface(IID_PPV_ARGS(&pDebugAdv))))
						{
							CONTEXT ctx = {};
							if (0 <= (hr = pDebugAdv->GetThreadContext(&ctx, sizeof(ctx))))
							{
								ULONG dw;
								ctx.Rsp -= sizeof(STACK);

								stack->Rip = ctx.Rip;

								if (0 <= (hr = pDataSpace->WriteVirtual(ctx.Rsp, stack, sizeof(STACK), &dw)))
								{
									ctx.Rip = func;

									if (0 > (hr = pDebugAdv->SetThreadContext(&ctx, sizeof(ctx))))
									{
										msg = "SetThreadContext FAIL";
									}
								}
							}
							
							pDebugAdv->Release();
						}
					}
				}
				else
				{
					msg = "!! PsLoadedModuleList == 0\n";
					hr = E_POINTER;
				}
			}

			pDataSpace->Release();
		}

__exit:

		if (msg)
		{
			pDebugControl->Output(DEBUG_OUTPUT_NORMAL, msg);
		}

		HMODULE hmod = 0;
		ULONG dwFlags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS;

		if (hr & FACILITY_NT_BIT)
		{
			hr &= ~FACILITY_NT_BIT;
			dwFlags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_IGNORE_INSERTS;
			hmod = GetModuleHandleW(L"ntdll");
		}

		if (FormatMessageA(dwFlags, hmod, hr, 0, (PSTR)&msg, 0, 0))
		{
			pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "%s\n", msg);
			LocalFree(const_cast<PSTR>(msg));
		}
		else
		{
			pDebugControl->Output(DEBUG_OUTPUT_NORMAL, "!! [%x]\n", hr);
		}

		pDebugControl->Release();
	}
}

void CALLBACK kdelay(IDebugClient* pDebugClient, PCSTR args)
{
	STACK stack;

	if (*args)
	{
		ULONG dwMilliseconds = strtoul(args, const_cast<PSTR*>(&args), 10);

		if (*args)
		{
			stack.Interval = MAXLONG_PTR;
		}

		stack.Interval = (LONGLONG)dwMilliseconds * -10000;
	}
	else
	{
		stack.Interval = MINLONGLONG;
	}

	common(pDebugClient, &stack, 1, __FUNCTION__);
}

void CALLBACK kalert(IDebugClient* pDebugClient, PCSTR args)
{
	STACK stack;

	stack.Thread = _strtoui64(args, const_cast<PSTR*>(&args), 16);

	if (stack.Thread <= 0xFFFF000000000000 )
	{
		stack.Thread = MAXLONG_PTR;
	}

	common(pDebugClient, &stack, 2, __FUNCTION__);
}

_NT_END