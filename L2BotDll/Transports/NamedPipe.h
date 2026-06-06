#pragma once

#include <Windows.h>
#include <string>
#include <memory>
#include <cstdint>
#include "Domain/Exceptions.h"
#include "Domain/Services/ServiceLocator.h"

#define BUFFER_SIZE 65536

	class NamedPipe
	{
	public:
		// Create a security descriptor that allows Everyone to access the pipe
		// This is needed because L2.exe runs as admin and Client.exe may not
		static SECURITY_ATTRIBUTES* GetPipeSecurityAttributes()
		{
			static SECURITY_ATTRIBUTES sa;
			static SECURITY_DESCRIPTOR sd;
			static bool initialized = false;
			if (!initialized) {
				initialized = true;
				InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
				// Grant full access to Everyone (S-1-1-0)
				SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
				sa.nLength = sizeof(SECURITY_ATTRIBUTES);
				sa.lpSecurityDescriptor = &sd;
				sa.bInheritHandle = FALSE;
			}
			return &sa;
		}

	const bool Connect(const std::wstring& pipeName)
	{
		// Close existing pipe if any
		if (m_Pipe != NULL)
		{
			FlushFileBuffers(m_Pipe);
			DisconnectNamedPipe(m_Pipe);
			CloseHandle(m_Pipe);
			m_Pipe = NULL;
		}

		m_Pipe = CreateNamedPipeW((L"\\\\.\\pipe\\" + pipeName).c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			BUFFER_SIZE * sizeof(wchar_t),
			BUFFER_SIZE * sizeof(wchar_t),
			NMPWAIT_USE_DEFAULT_WAIT,
			GetPipeSecurityAttributes()
		);

		if (m_Pipe == INVALID_HANDLE_VALUE)
		{
			DWORD err = GetLastError();
			throw CriticalRuntimeException(L"cannot create the pipe " + pipeName + L": " + std::to_wstring(err));
		}

		m_PipeName = pipeName;

		// Overlapped ConnectNamedPipe — wait for client to connect
		OVERLAPPED ol = {};
		ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (ol.hEvent == NULL) {
			throw CriticalRuntimeException(L"cannot create connect event");
		}

		BOOL connected = ConnectNamedPipe(m_Pipe, &ol);
		if (!connected) {
			DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING) {
			// Wait for client connection (blocking in Connect thread)
				WaitForSingleObject(ol.hEvent, INFINITE);
				DWORD bytesTransferred = 0;
				GetOverlappedResult(m_Pipe, &ol, &bytesTransferred, FALSE);
		} else if (err != ERROR_PIPE_CONNECTED) {
			CloseHandle(ol.hEvent);
				m_Connected = false;
				throw CriticalRuntimeException(L"cannot connect the pipe " + m_PipeName + L": " + std::to_wstring(err));
			}
		}
	CloseHandle(ol.hEvent);

	m_Connected = true;
		return true;
	}

	void Send(const std::wstring& message)
	{
		if (!m_Connected)
		{
			return;
		}

		OVERLAPPED ol = {};
		ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (ol.hEvent == NULL) {
			m_Connected = false;
			return;
		}

		DWORD written = 0;
		BOOL ok = WriteFile(m_Pipe, message.c_str(), (DWORD)((message.size() + 1) * sizeof(wchar_t)), &written, &ol);
		if (!ok) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				DWORD waitResult = WaitForSingleObject(ol.hEvent, 1000);
				if (waitResult == WAIT_OBJECT_0) {
					DWORD bytesTransferred = 0;
					GetOverlappedResult(m_Pipe, &ol, &bytesTransferred, FALSE);
					CloseHandle(ol.hEvent);
					return;
				}
				CancelIo(m_Pipe);
				CloseHandle(ol.hEvent);
				m_Connected = false;
				return;
			}
			// Pipe errors (BROKEN_PIPE, etc.) = client gone.
			// Set disconnected, let Connect thread handle reconnection.
			// Do NOT throw CriticalRuntimeException — it used to kill the DLL.
			CloseHandle(ol.hEvent);
			m_Connected = false;
			return;
		}
		CloseHandle(ol.hEvent);
	}

	// Flush the pipe output buffer — pushes all pending data to the client.
	// Must be called AFTER all messages are written in a batch.
	// WARNING: This can block if the client isn't reading. Call from a context
	// that can tolerate a brief block (e.g. after StartCurrentProcess when
	// L2 threads are running). Keep a short timeout by calling from a thread
	// that can be interrupted.
	void FlushOutput()
	{
		if (m_Pipe != NULL && m_Pipe != INVALID_HANDLE_VALUE && m_Connected)
		{
			FlushFileBuffers(m_Pipe);
		}
	}

	void Flush()
	{
		if (m_Pipe != NULL && m_Pipe != INVALID_HANDLE_VALUE)
		{
			FlushFileBuffers(m_Pipe);
		}
	}

	void Close()
	{
		if (m_Pipe != NULL && m_Pipe != INVALID_HANDLE_VALUE)
		{
			FlushFileBuffers(m_Pipe);
			DisconnectNamedPipe(m_Pipe);
			CloseHandle(m_Pipe);
			m_Pipe = NULL;
			m_Connected = false;
		}
	}

		const std::wstring Receive()
	{
		if (!m_Connected)
		{
			return L"";
		}

		OVERLAPPED ol = {};
		ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (ol.hEvent == NULL) {
			m_Connected = false;
			return L"";
		}

		DWORD dwRead = 0;
		std::unique_ptr<wchar_t[]> buffer = std::make_unique<wchar_t[]>(BUFFER_SIZE);
		BOOL ok = ReadFile(m_Pipe, buffer.get(), BUFFER_SIZE * sizeof(wchar_t), &dwRead, &ol);
		if (!ok) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				DWORD waitResult = WaitForSingleObject(ol.hEvent, 200);
				if (waitResult == WAIT_OBJECT_0) {
					DWORD bytesTransferred = 0;
					GetOverlappedResult(m_Pipe, &ol, &bytesTransferred, FALSE);
					dwRead = bytesTransferred;
				} else {
					CancelIo(m_Pipe);
					CloseHandle(ol.hEvent);
					return L"";
				}
			} else {
				// Pipe errors (ERROR_BROKEN_PIPE, etc.) = client gone.
				// Set disconnected, let Connect thread handle reconnection.
				// Do NOT throw CriticalRuntimeException — it used to kill the DLL.
				CloseHandle(ol.hEvent);
				m_Connected = false;
				return L"";
			}
		}

		CloseHandle(ol.hEvent);

		if (dwRead == 0)
		{
			return L"";
		}

		return std::wstring(buffer.get());
	}

	const bool IsConnected() const
	{
		return m_Connected;
	}

	virtual ~NamedPipe()
	{
		if (m_Pipe != NULL)
		{
			DisconnectNamedPipe(m_Pipe);
			CloseHandle(m_Pipe);
		}
	}
	NamedPipe() = default;

private:
	std::wstring m_PipeName = L"";
	HANDLE m_Pipe = NULL;
	bool m_Connected = false;
};
