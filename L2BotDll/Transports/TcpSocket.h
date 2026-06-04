#pragma once

#include <WinSock2.h>
#include <string>
#include <memory>
#include <cstdint>
#include "Domain/Exceptions.h"
#include "Domain/Services/ServiceLocator.h"

#pragma comment(lib, "ws2_32.lib")

#define TCP_BUFFER_SIZE 16384

class TcpSocket
{
public:
	const bool Connect(const std::wstring& portStr)
	{
		// Clean up any existing connection
		Disconnect();

		// Lazy WSA init — only once per process
		if (!s_WsaInitialized)
		{
			WSADATA wsaData;
			const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (wsaResult != 0)
			{
				throw CriticalRuntimeException(L"WSAStartup failed: " + std::to_wstring(wsaResult));
			}
			s_WsaInitialized = true;
		}

		// Parse port number from string
		const int port = std::stoi(portStr);

		// Create listening socket
		SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSock == INVALID_SOCKET)
		{
			throw CriticalRuntimeException(L"socket() failed: " + std::to_wstring(WSAGetLastError()));
		}

		// Allow address reuse so restart doesn't fail with TIME_WAIT
		const int optval = 1;
		setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

		// Bind to 127.0.0.1:port
		sockaddr_in serverAddr = {};
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		serverAddr.sin_port = htons((u_short)port);

		if (bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
		{
			const int err = WSAGetLastError();
			closesocket(listenSock);
			throw CriticalRuntimeException(L"bind() failed on port " + portStr + L": " + std::to_wstring(err));
		}

		if (listen(listenSock, 1) == SOCKET_ERROR)
		{
			const int err = WSAGetLastError();
			closesocket(listenSock);
			throw CriticalRuntimeException(L"listen() failed: " + std::to_wstring(err));
		}

		m_ListenSocket = listenSock;
		m_Port = portStr;

		// Blocking call — waits in the Connect thread until a client connects
		m_ClientSocket = accept(listenSock, NULL, NULL);
		if (m_ClientSocket == INVALID_SOCKET)
		{
			const int err = WSAGetLastError();
			closesocket(m_ListenSocket);
			m_ListenSocket = INVALID_SOCKET;
			m_Connected = false;
			throw CriticalRuntimeException(L"accept() failed on port " + m_Port + L": " + std::to_wstring(err));
		}

		// Client connected — no longer need the listening socket
		closesocket(m_ListenSocket);
		m_ListenSocket = INVALID_SOCKET;

		m_Connected = true;
		return true;
	}

	void Send(const std::wstring& message)
	{
		if (!m_Connected || m_ClientSocket == INVALID_SOCKET)
		{
			return;
		}

		// Send as UTF-8 with newline terminator
		const std::string utf8 = WideToUtf8(message + L"\n");
		const int totalBytes = (int)utf8.size();
		int sent = 0;

		while (sent < totalBytes)
		{
			const int result = ::send(m_ClientSocket, utf8.data() + sent, totalBytes - sent, 0);
			if (result == SOCKET_ERROR)
			{
				m_Connected = false;
				throw RuntimeException(L"send() failed: " + std::to_wstring(WSAGetLastError()));
			}
			sent += result;
		}
	}

	const std::wstring Receive()
	{
		if (!m_Connected || m_ClientSocket == INVALID_SOCKET)
		{
			return L"";
		}

		// Read until we find a newline (message framing)
		std::string line;
		while (m_RecvBuffer.find('\n') == std::string::npos)
		{
			char buf[TCP_BUFFER_SIZE];
			const int bytesRead = recv(m_ClientSocket, buf, sizeof(buf) - 1, 0);
			if (bytesRead == SOCKET_ERROR)
			{
				m_Connected = false;
				throw RuntimeException(L"recv() failed: " + std::to_wstring(WSAGetLastError()));
			}
			if (bytesRead == 0)
			{
				// Client disconnected gracefully
				m_Connected = false;
				throw RuntimeException(L"client disconnected");
			}
			buf[bytesRead] = '\0';
			m_RecvBuffer.append(buf, bytesRead);
		}

		// Extract one complete line (up to and including newline)
		const size_t newlinePos = m_RecvBuffer.find('\n');
		std::string completeMsg = m_RecvBuffer.substr(0, newlinePos + 1);
		m_RecvBuffer = m_RecvBuffer.substr(newlinePos + 1);

		// Trim the newline
		while (!completeMsg.empty() && (completeMsg.back() == '\n' || completeMsg.back() == '\r'))
		{
			completeMsg.pop_back();
		}

		if (completeMsg.empty())
		{
			return L"";
		}

		return Utf8ToWide(completeMsg);
	}

	const bool IsConnected() const
	{
		return m_Connected;
	}

	virtual ~TcpSocket()
	{
		Disconnect();
		if (s_WsaInitialized)
		{
			WSACleanup();
			s_WsaInitialized = false;
		}
	}
	TcpSocket() = default;

private:
	void Disconnect()
	{
		if (m_ClientSocket != INVALID_SOCKET)
		{
			shutdown(m_ClientSocket, SD_BOTH);
			closesocket(m_ClientSocket);
			m_ClientSocket = INVALID_SOCKET;
		}
		if (m_ListenSocket != INVALID_SOCKET)
		{
			closesocket(m_ListenSocket);
			m_ListenSocket = INVALID_SOCKET;
		}
		m_Connected = false;
		m_RecvBuffer.clear();
	}

	static std::string WideToUtf8(const std::wstring& wide)
	{
		if (wide.empty()) return "";
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
		if (len == 0) return "";
		std::string result(len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], len, NULL, NULL);
		return result;
	}

	static std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
		if (len == 0) return L"";
		std::wstring result(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &result[0], len);
		return result;
	}

private:
	SOCKET m_ListenSocket = INVALID_SOCKET;
	SOCKET m_ClientSocket = INVALID_SOCKET;
	std::wstring m_Port = L"";
	bool m_Connected = false;
	std::string m_RecvBuffer;

	inline static bool s_WsaInitialized = false;
};
