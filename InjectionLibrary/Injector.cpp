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