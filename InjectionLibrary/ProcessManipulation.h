#pragma once

#include <windows.h>
#include <string>
#include <set>

namespace InjectLibrary
{
	void StartProcess(const DWORD processId);
	void StartCurrentProcess();
	void StopProcess(const DWORD processId);
	void StopCurrentProcess();
	// Skip freezing threads in the excluded set (e.g. DLL pipe threads)
	void StopCurrentProcessExcluding(const std::set<DWORD>& excludeThreadIds);
	const std::string GetProcessName(const DWORD processId);
	const std::string GetCurrentProcessName();
};
