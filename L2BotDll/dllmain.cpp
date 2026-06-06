#include "pch.h"
#include "Common/apihook.h"
#include "Application.h"
#include "ProcessManipulation.h"
#include "Injector.h"

InjectLibrary::Injector injector("L2BotHookMutex", WH_GETMESSAGE);
Application application(VersionAbstractFactory::Version::interlude);

static HANDLE g_hInitThread = NULL;
static bool g_isLineageProcess = false;

void ConfigLogger(HMODULE hModule);

// Temp debug: log to E:\L2Teon\system\dll_debug.log
static void DllDbg(const char* msg)
{
	FILE* f = nullptr;
	errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\dll_debug.log", L"a");
	if (err == 0 && f) {
		fprintf(f, "%s\n", msg);
		fflush(f);
		fclose(f);
	}
}

static void DllDbgFmt(const char* fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
	va_end(args);
	DllDbg(buf);
}

// Run heavy initialization on a separate thread to avoid loader lock deadlock.
// DllMain is called inside the loader lock — we MUST NOT create threads,
// wait on objects, or create named pipes inside it.
DWORD WINAPI InitThreadProc(LPVOID lpParam)
{
	// Give the loader lock time to release
	Sleep(500);

	HMODULE hModule = (HMODULE)lpParam;

	const int processId = GetCurrentProcessId();
	const std::string& processName = InjectLibrary::GetCurrentProcessName();
	const bool isLineage = processName == "l2.exe" || processName == "l2.bin" || processName == "L2.exe" || processName == "L2.bin" || GetModuleHandleW(L"Engine.dll") != NULL;
	DllDbgFmt("[Init] processName='%s' pid=%d isLineage=%d Engine=0x%p", processName.c_str(), processId, isLineage, GetModuleHandleW(L"Engine.dll"));

	ConfigLogger(hModule);

	InjectLibrary::StopCurrentProcess();
	DllDbg("[Init] StopCurrentProcess done");

	application.Start();
	DllDbg("[Init] application.Start() done");

	InjectLibrary::StartCurrentProcess();
	DllDbg("[Init] StartCurrentProcess done");

	return 0;
}

void ConfigLogger(HMODULE hModule)
{
	wchar_t buf[MAX_PATH];
	GetModuleFileNameW(hModule, buf, MAX_PATH);
	const std::wstring libName(buf);

	std::wstring directory;
	const size_t lastSlashIndex = libName.rfind(L"\\");
	if (std::string::npos != lastSlashIndex)
	{
		directory = libName.substr(0, lastSlashIndex);
	}

	std::vector <std::unique_ptr<Logger::LogChannel>> channels;
#ifdef _DEBUG
	channels.push_back(std::make_unique<OutputDebugLogChannel>(std::vector<Logger::LogLevel>{}));
#endif
	channels.push_back(std::make_unique<FileLogChannel>(directory + L"\\app.log", std::vector<Logger::LogLevel>{
#ifndef _DEBUG
		Logger::LogLevel::error,
		Logger::LogLevel::warning,
		Logger::LogLevel::info
#endif
	}));
	channels.push_back(std::make_unique<ChatLogChannel>(Enums::ChatChannelEnum::log, std::vector<Logger::LogLevel>{
		Logger::LogLevel::app
	}));
	Services::ServiceLocator::GetInstance().SetLogger(std::make_unique<Logger::Logger>(std::move(channels)));
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	const int processId = GetCurrentProcessId();
	const std::string& processName = InjectLibrary::GetCurrentProcessName();

	const bool isLineageProcess = processName == "l2.exe" || processName == "l2.bin" || processName == "L2.exe" || processName == "L2.bin" || GetModuleHandleW(L"Engine.dll") != NULL;

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DllDbgFmt("[DllMain] ATTACH processName='%s' isLineage=%d Engine=0x%p", processName.c_str(), isLineageProcess, GetModuleHandleW(L"Engine.dll"));
		injector.SetHook(hModule);
		if (isLineageProcess) {
			g_isLineageProcess = true;
			g_hInitThread = CreateThread(NULL, 0, InitThreadProc, hModule, 0, NULL);
			DllDbgFmt("[DllMain] InitThread created handle=0x%p", g_hInitThread);
		} else {
			DllDbg("[DllMain] NOT a Lineage process - skipping init");
		}
		break;
	case DLL_PROCESS_DETACH:
		if (g_isLineageProcess) {
			// Don't use StopCurrentProcess here — it would suspend our
			// own Send/Receive/Connect threads, causing deadlock when
			// application.Stop() tries to join them.
			application.Stop();
		}
		if (g_hInitThread) {
			WaitForSingleObject(g_hInitThread, 3000);
			CloseHandle(g_hInitThread);
		}
		injector.SetHook();
		break;
	}
	return TRUE;
}
