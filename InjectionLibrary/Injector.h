#pragma once

#include <windows.h>
#include <string>
#include <functional>

namespace InjectLibrary
{
	class Injector
	{
	public:
		Injector(const std::string& mutexName, int windowsMessage);
		virtual ~Injector() = default;
		void CALLBACK SetHook(const HINSTANCE moduleHandle = nullptr);
		// Set a callback that will be called from the hook procedure
		// (running on L2's UI thread) to safely read game state.
		static void SetHookCallback(std::function<void()> callback);
	private:
		static const LRESULT CALLBACK HookMessageProcedure(const DWORD code, const DWORD wParam, const DWORD lParam);

	private:
		static HHOOK _hookHandle;
		static std::function<void()> _hookCallback;
		HANDLE _mutexHandle = nullptr;
		int _windowsMessage = 0;
		const std::string _mutexName;
	};
};
