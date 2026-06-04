#pragma once
#include "Domain/Transports/TransportInterface.h"
#include <Windows.h>
#include "TcpSocket.h"
#include "../Common/Common.h"
#include "Domain/Services/ServiceLocator.h"

using namespace L2Bot::Domain;

class TcpSocketTransport : public Transports::TransportInterface
{
public:
	const bool Connect() override
	{
		// TCP server: listen on 127.0.0.1:port, block until client connects
		if (!m_Socket.Connect(m_Port))
		{
			return false;
		}

		// Send greeting — client expects "Hello!" as first message
		m_Socket.Send(L"Hello!");

		return true;
	}

	const void Send(const std::wstring& data) override
	{
		if (!m_Socket.IsConnected())
		{
			return;
		}

		m_Socket.Send(data);
	}

	const std::wstring Receive() override
	{
		if (!m_Socket.IsConnected())
		{
			return L"";
		}

		return m_Socket.Receive();
	}

	const bool IsConnected() const override
	{
		return m_Socket.IsConnected();
	}

	TcpSocketTransport(const std::wstring& port) :
		m_Port(port)
	{
	}

	TcpSocketTransport() = delete;
	virtual ~TcpSocketTransport() = default;

private:
	TcpSocket m_Socket;
	std::wstring m_Port = L"5816";
};
