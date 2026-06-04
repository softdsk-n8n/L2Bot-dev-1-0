#include "pch.h"
#include <Windows.h>
#include <cstdio>
#include <csetjmp>

namespace SEHHelper
{
	static jmp_buf g_JmpBuf;
	static bool g_InTest = false;

	static LONG CALLBACK RepoSafeVEH(PEXCEPTION_POINTERS pExInfo)
	{
		if (g_InTest) {
			longjmp(g_JmpBuf, (int)pExInfo->ExceptionRecord->ExceptionCode);
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}

	static void WriteDiagRaw(const char* msg)
	{
		HANDLE hF = CreateFileW(L"E:\\L2Teon\\system\\seh_debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		if (hF != INVALID_HANDLE_VALUE) {
			SYSTEMTIME st; GetLocalTime(&st);
			char header[32];
			int hlen = sprintf_s(header, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
			DWORD written;
			WriteFile(hF, header, (DWORD)hlen, &written, NULL);
			WriteFile(hF, msg, (DWORD)strlen(msg), &written, NULL);
			WriteFile(hF, "\n", 1, &written, NULL);
			CloseHandle(hF);
		}
	}

	// Returns 0 if safe, exception code if SEH crash
	extern "C" DWORD SEHHelper_TestRepoSafeWithVEH(void (*fn)())
	{
		g_InTest = true;
		void* vehHandle = AddVectoredExceptionHandler(0, RepoSafeVEH); // 0 = last in chain

		DWORD result = 0;
		int jmpVal = setjmp(g_JmpBuf);
		if (jmpVal == 0) {
			// Normal path - call the function
			fn();
		} else {
			// Exception occurred - longjmp'd here
			result = (DWORD)jmpVal;
			char buf[256];
			sprintf_s(buf, "TestRepoSafeVEH caught 0x%08X", result);
			WriteDiagRaw(buf);
		}

		if (vehHandle) RemoveVectoredExceptionHandler(vehHandle);
		g_InTest = false;
		return result;
	}
}
