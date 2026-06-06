#include "pch.h"
#include "../../../Common/apihook.h"
#include "L2GameDataWrapper.h"
#include "ProcessManipulation.h"
#include "Domain/Services/ServiceLocator.h"
#include "Domain/Exceptions.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	void* L2GameDataWrapper::originalInitAddress = 0;
	L2GameDataWrapper::L2GameData* L2GameDataWrapper::_target = 0;

	int(__thiscall* L2GameDataWrapper::__Init)(L2GameData*, int, int) = 0;
	FL2ItemDataBase* (__thiscall* L2GameDataWrapper::__GetItemData)(L2GameData*, int) = 0;
	FL2MagicSkillData* (__thiscall* L2GameDataWrapper::__GetMSData)(L2GameData*, int, int) = 0;

	// Follow JMP thunk (E9 rel32) to get real function address
	static FARPROC ResolveThunk(FARPROC thunkAddr)
	{
		if (thunkAddr == 0) return 0;
		unsigned char* p = (unsigned char*)thunkAddr;
		if (p[0] == 0xE9) {
			int32_t offset = *(int32_t*)(p + 1);
			return (FARPROC)(p + 5 + offset);
		}
		return thunkAddr;
	}

	// SEH-safe: probe a single pointer to check if it points to an object
	// with the expected vtable.
	static bool ProbeVtableMatch(uintptr_t ptr, uintptr_t expectedVtable)
	{
		__try {
			return *(uintptr_t*)ptr == expectedVtable;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Scan a readable memory region for a pointer to an object whose vtable matches.
	// Caller must ensure the region [startAddr, endAddr) is readable.
	static uintptr_t ScanReadableRegionForVtable(uintptr_t startAddr, uintptr_t endAddr, uintptr_t expectedVtable)
	{
		__try {
			for (uintptr_t addr = startAddr; addr + sizeof(uintptr_t) <= endAddr; addr += sizeof(uintptr_t)) {
				uintptr_t ptr = *(uintptr_t*)addr;
				if (ptr == 0 || ptr < 0x10000) continue;
				if (*(uintptr_t*)ptr == expectedVtable) {
					return ptr;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Some pointer in this region was bad — fall back to per-probe scan
			for (uintptr_t addr = startAddr; addr + sizeof(uintptr_t) <= endAddr; addr += sizeof(uintptr_t)) {
				uintptr_t ptr = *(uintptr_t*)addr;
				if (ptr == 0 || ptr < 0x10000) continue;
				if (ProbeVtableMatch(ptr, expectedVtable)) {
					return ptr;
				}
			}
		}
		return 0;
	}

	// Scan all committed writable memory for a pointer to an object with expected vtable.
	// Uses VirtualQuery to enumerate regions, then scans each readable region.
	// Continues scanning after false positives (GetItemData crash).
	static uintptr_t ScanAllMemoryForVtable(uintptr_t expectedVtable, FARPROC getItemDataFn, FILE* diag)
	{
		MEMORY_BASIC_INFORMATION mbi;
		uintptr_t addr = 0;
		int regionsScanned = 0;
		int candidatesFound = 0;
		
		while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != 0) {
			if (mbi.State == MEM_COMMIT && 
				(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
				!(mbi.Protect & PAGE_GUARD) &&
				mbi.RegionSize < 0x10000000) {
				
				uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
				uintptr_t regionEnd = regionStart + mbi.RegionSize;
				
				// Scan this region for all vtable matches
				__try {
					for (uintptr_t rAddr = regionStart; rAddr + sizeof(uintptr_t) <= regionEnd; rAddr += sizeof(uintptr_t)) {
						uintptr_t ptr = *(uintptr_t*)rAddr;
						if (ptr == 0 || ptr < 0x10000) continue;
						
						bool isMatch = false;
						__try {
							isMatch = (*(uintptr_t*)ptr == expectedVtable);
						} __except(EXCEPTION_EXECUTE_HANDLER) {
							continue; // bad pointer, skip
						}
						
						if (!isMatch) continue;
						
						candidatesFound++;
						// Verify with GetItemData
						__try {
							if (getItemDataFn) {
								typedef FL2ItemDataBase* (__thiscall *GetItemDataFn)(uintptr_t, int);
								GetItemDataFn fn = (GetItemDataFn)getItemDataFn;
								FL2ItemDataBase* result = fn(ptr, 57);
								if (result != 0) {
									if (diag) { fprintf(diag, "[ScanAllMemory] VERIFIED singleton=0x%x (candidate #%d in region 0x%x)\n",
										(int)ptr, candidatesFound, (int)regionStart); fflush(diag); }
									return ptr;
								} else {
									if (diag) { fprintf(diag, "[ScanAllMemory] candidate 0x%x vtable match but GetItemData returned null\n", (int)ptr); fflush(diag); }
								}
							}
						} __except(EXCEPTION_EXECUTE_HANDLER) {
							if (diag) { fprintf(diag, "[ScanAllMemory] candidate 0x%x crashed on GetItemData — false positive\n", (int)ptr); fflush(diag); }
						}
					}
				} __except(EXCEPTION_EXECUTE_HANDLER) {
					// Region scan failed entirely — skip
				}
				
				regionsScanned++;
				
				if (diag && regionsScanned % 500 == 0) {
					fprintf(diag, "[ScanAllMemory] scanned %d regions, %d candidates, current 0x%x\n",
						regionsScanned, candidatesFound, (int)regionStart);
					fflush(diag);
				}
			}
			
			addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
			if (addr < (uintptr_t)mbi.BaseAddress) break;
		}
		
		if (diag) { fprintf(diag, "[ScanAllMemory] done: %d regions, %d candidates — none verified\n", regionsScanned, candidatesFound); fflush(diag); }
		return 0;
	}

	void L2GameDataWrapper::Init(HMODULE hModule)
	{
		FILE* diag = nullptr;
		errno_t err = _wfopen_s(&diag, L"E:\\L2Teon\\system\\dll_debug.log", L"a");
		
		(FARPROC&)__GetItemData = GetProcAddress(hModule, "?GetItemData@FL2GameData@@QAEPAVFL2ItemDataBase@@H@Z");
		(FARPROC&)__GetMSData = GetProcAddress(hModule, "?GetMSData@FL2GameData@@QAEPAUFL2MagicSkillData@@HH@Z");

		if (diag) {
			fprintf(diag, "[L2GameData::Init] hModule=0x%x GetItemData=0x%x GetMSData=0x%x\n",
				(int)hModule, (int)__GetItemData, (int)__GetMSData);
			fflush(diag);
		}

		if (!__GetItemData) {
			if (diag) { fprintf(diag, "[L2GameData::Init] FAIL: GetItemData export NOT found\n"); fflush(diag); fclose(diag); }
			return;
		}

		uintptr_t base = (uintptr_t)hModule;
		FARPROC realGetItemData = ResolveThunk((FARPROC)__GetItemData);

		if (diag) {
			fprintf(diag, "[L2GameData::Init] Engine base=0x%x GetItemData thunk=0x%x real=0x%x\n",
				(int)base, (int)__GetItemData, (int)realGetItemData);
			fflush(diag);
		}

		(FARPROC&)__GetItemData = realGetItemData;

		// STRATEGY: Find FL2GameData singleton via vtable match.
		// The vtable symbol ??_7FL2GameData@@6B@ is exported by Engine.dll.
		// We get its address, then scan Engine.dll's writable sections for a
		// pointer to an object whose first dword (vtable ptr) matches.
		
		if (_target == 0 && hModule) {
			FARPROC vtableProc = GetProcAddress(hModule, "??_7FL2GameData@@6B@");
			uintptr_t vtableAddr = (uintptr_t)vtableProc;
			
			if (diag) {
				fprintf(diag, "[L2GameData::Init] Vtable export=0x%x (RVA=0x%x)\n",
					(int)vtableAddr, (int)(vtableAddr - base));
				fflush(diag);
			}

			if (vtableAddr != 0) {
				// First try Engine.dll .data sections (fast, small)
				IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)base;
				if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
					IMAGE_NT_HEADERS* ntHeader = (IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);
					if (ntHeader->Signature == IMAGE_NT_SIGNATURE) {
						IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeader);
						for (int i = 0; i < ntHeader->FileHeader.NumberOfSections; i++, section++) {
							DWORD characteristics = section->Characteristics;
							if ((characteristics & IMAGE_SCN_MEM_WRITE) && 
								!(characteristics & IMAGE_SCN_MEM_EXECUTE)) {
								
								uintptr_t dataStart = base + section->VirtualAddress;
								uintptr_t dataEnd = dataStart + section->Misc.VirtualSize;
								
								if (diag) { fprintf(diag, "[L2GameData::Init] Scanning Engine .data section %d: 0x%x-0x%x for vtable=0x%x\n",
									i, (int)dataStart, (int)dataEnd, (int)vtableAddr); fflush(diag); }

								uintptr_t found = ScanReadableRegionForVtable(dataStart, dataEnd, vtableAddr);
								if (found != 0) {
									_target = (L2GameData*)found;
									if (diag) { fprintf(diag, "[L2GameData::Init] FOUND in Engine .data: singleton=0x%x\n", (int)_target); fflush(diag); }
									
									__try {
										FL2ItemDataBase* testResult = (*__GetItemData)(_target, 57);
										if (diag) { fprintf(diag, "[L2GameData::Init] GetItemData(57)=0x%x\n", (int)testResult); fflush(diag); }
									} __except(EXCEPTION_EXECUTE_HANDLER) {
										if (diag) { fprintf(diag, "[L2GameData::Init] GetItemData(57) CRASHED\n"); fflush(diag); }
										_target = 0;
									}
									
									if (_target != 0) { if (diag) fclose(diag); return; }
								}
							}
						}
					}
				}

				if (diag) { fprintf(diag, "[L2GameData::Init] Engine .data scan failed — scanning ALL memory (slower)\n"); fflush(diag); }
				
				// Broader scan: entire process memory (heap, other DLLs, etc.)
				uintptr_t found = ScanAllMemoryForVtable(vtableAddr, (FARPROC)__GetItemData, diag);
				if (found != 0) {
					_target = (L2GameData*)found;
					if (diag) { fprintf(diag, "[L2GameData::Init] VERIFIED singleton=0x%x\n", (int)_target); fflush(diag); fclose(diag); }
					return;
				}
			} else {
				if (diag) { fprintf(diag, "[L2GameData::Init] Vtable export NOT found - cannot scan\n"); fflush(diag); }
			}

			if (_target == 0 && diag) { fprintf(diag, "[L2GameData::Init] ALL SCANS FAILED - _target=0\n"); fflush(diag); }
		}

		// Init hook - note: Teon's Engine.dll does NOT export ?Init@FL2GameData@@QAEHHH@Z
		FARPROC initProc = GetProcAddress(hModule, "?Init@FL2GameData@@QAEHHH@Z");
		if (initProc) {
			FARPROC realInit = ResolveThunk(initProc);
			originalInitAddress = realInit;
			(FARPROC&)__Init = (FARPROC)splice(realInit, (void*)&__Init_hook);
			if (diag) { fprintf(diag, "[L2GameData::Init] Init splice installed (thunk=0x%x real=0x%x)\n", (int)initProc, (int)realInit); fflush(diag); }
		} else {
			if (diag) { fprintf(diag, "[L2GameData::Init] Init export NOT FOUND (expected for Teon)\n"); fflush(diag); }
		}

		if (diag) fclose(diag);
	}

	void L2GameDataWrapper::Restore()
	{
		FILE* diag = nullptr;
		errno_t err = _wfopen_s(&diag, L"E:\\L2Teon\\system\\dll_debug.log", L"a");
		if (diag) { fprintf(diag, "[L2GameData::Restore] _target=0x%x\n", (int)_target); fflush(diag); fclose(diag); }
	}

	FL2ItemDataBase* L2GameDataWrapper::GetItemData(int itemId) const
	{
		__try {
			if (__GetItemData && _target) {
				return (*__GetItemData)(_target, itemId);
			}
			static bool loggedOnce = false;
			if (!loggedOnce) {
				loggedOnce = true;
				FILE* f = nullptr;
				errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\drop_debug.log", L"a");
				if (err == 0 && f) {
					fprintf(f, "[GETITEMDATA] FAIL: _target=0x%x __GetItemData=0x%x - cannot load item data!\n",
						(int)_target, (int)__GetItemData);
					fflush(f); fclose(f);
				}
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			throw CriticalRuntimeException(L"FL2GameData::GetItemData failed");
		}
	}

	FL2MagicSkillData* L2GameDataWrapper::GetMSData(int skillId, int level) const
	{
		__try {
			if (__GetMSData && _target) {
				return (*__GetMSData)(_target, skillId, level);
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			throw CriticalRuntimeException(L"FL2GameData::GetMSData failed");
		}
	}

	int __fastcall L2GameDataWrapper::__Init_hook(L2GameData* This, int, int unk, int unk1)
	{
		if (_target == 0 && This != 0) {
			_target = This;
			FILE* f = nullptr;
			errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\dll_debug.log", L"a");
			if (err == 0 && f) {
				fprintf(f, "[L2GameData::Init_hook] CAPTURED _target=0x%x\n", (int)_target);
				fflush(f); fclose(f);
			}
		}

		return __Init(This, unk, unk1);
	}
}
