#include "pch.h"
#include "Injector.h"

namespace InjectLibrary
{
	HHOOK Injector::_hookHandle = nullptr;
	std::function<void()> Injector::_hookCallback;

	Injector::Injector(const std::string& mutexName, int windowsMessage) : _mutexName(mutexName), _windowsMessage(windowsMessage)
	{
	}

	void Injector::SetHookCallback(std::function<void()> callback)
	{
		_hookCallback = callback;
	}

	void Injector::SetHook(const HINSTANCE moduleHandle)
	{
		if (moduleHandle) {
			// Always install the hook. The mutex check was preventing
			// re-installation after L2 restart because the named mutex
			// from a previous session might linger in the OS namespace.
			// Close any existing mutex first.
			if (_mutexHandle) {
				CloseHandle(_mutexHandle);
				_mutexHandle = nullptr;
			}
			if (_hookHandle) {
				UnhookWindowsHookEx(_hookHandle);
				_hookHandle = nullptr;
			}

			_hookHandle = SetWindowsHookExA(_windowsMessage, (HOOKPROC)HookMessageProcedure, moduleHandle, 0);
			_mutexHandle = CreateMutexA(nullptr, false, _mutexName.c_str());

			FILE* f = nullptr;
			errno_t ferr = _wfopen_s(&f, L"E:\\L2Teon\\system\\debug_init.log", L"a");
			if (ferr == 0 && f) { fprintf(f, "[SetHook] hookHandle=0x%p mutexHandle=0x%p\n", _hookHandle, _mutexHandle); fflush(f); fclose(f); }
		}
		else {
			UnhookWindowsHookEx(_hookHandle);
			_hookHandle = nullptr;
			if (_mutexHandle) {
				CloseHandle(_mutexHandle);
				_mutexHandle = nullptr;
			}
		}
	}

	const LRESULT Injector::HookMessageProcedure(const DWORD code, const DWORD wParam, const DWORD lParam)
	{
		// Call the registered callback (game state read) on L2's UI thread.
		// This is the ONLY safe place to call game engine functions like
		// GetNextCreature because we're inside L2's message loop where
		// game mutexes are not held against us.
		if (_hookCallback) {
			_hookCallback();
		}
		return CallNextHookEx(_hookHandle, code, wParam, lParam);
	}
}