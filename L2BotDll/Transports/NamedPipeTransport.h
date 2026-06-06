#pragma once
#include "Domain/Transports/TransportInterface.h"
#include <Windows.h>
#include "NamedPipe.h"
#include "../Common/Common.h"
#include "Domain/Services/ServiceLocator.h"


using namespace L2Bot::Domain;

	class NamedPipeTransport : public Transports::TransportInterface
{
public:
	const bool Connect() override
	{
		if (!m_ConnectionPipe.Connect(m_PipeName))
		{
			return false;
		}

		const auto mainPipeName = GenerateUUID();
		m_ConnectionPipe.Send(mainPipeName);
		m_ConnectionPipe.Flush();
		// Give the Client time to read the UUID before we disconnect.
		// Without this delay, DisconnectNamedPipe() in Close() destroys
		// the pipe before Client's synchronous Read() completes, causing
		// Read() to return 0 bytes → empty pipeName → handshake fails.
		Sleep(2000);
		m_ConnectionPipe.Close();

		if (!m_Pipe.Connect(mainPipeName))
		{
			return false;
		}

		m_Pipe.Send(L"Hello!");
		return true;
	}

	const void Send(const std::wstring& data) override
	{
		if (!m_Pipe.IsConnected())
		{
			return;
		}

		m_Pipe.Send(data);
	}

	// Flush output after batch of messages. See NamedPipe::FlushOutput().
	void FlushOutput()
	{
		m_Pipe.FlushOutput();
	}

	const std::wstring Receive() override
	{
		if (!m_Pipe.IsConnected())
		{
			return L"";
		}

		return m_Pipe.Receive();
	}

	const bool IsConnected() const override
	{
		return m_Pipe.IsConnected();
	}

	NamedPipeTransport(const std::wstring& pipeName) :
		m_PipeName(pipeName)
	{
	}

	NamedPipeTransport() = delete;
	virtual ~NamedPipeTransport() = default;

private:
	NamedPipe m_ConnectionPipe;
	NamedPipe m_Pipe;
	std::wstring m_PipeName = L"";
};