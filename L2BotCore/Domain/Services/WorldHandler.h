#pragma once

#include <cstdint>
#include <thread>
#include <memory>
#include <Windows.h>
#include "../Serializers/SerializerInterface.h"
#include "../Repositories/EntityRepositoryInterface.h"
#include "../Transports/TransportInterface.h"
#include "../DTO/Message.h"
#include "../Services/IncomingMessageProcessor.h"
#include "../Services/OutgoingMessageBuilder.h"
#include "../Exceptions.h"
#include "../Services/ServiceLocator.h"
#include "../../../InjectionLibrary/ProcessManipulation.h"
#include "../../../L2BotDll/Versions/Interlude/Services/HeroService.h"
#include <set>
#include <mutex>
#include <functional>
#include <vector>
#include <atomic>

namespace L2Bot::Domain::Services
{
	class WorldHandler
	{
	public:
		WorldHandler(
			const std::map<std::wstring, Repositories::EntityRepositoryInterface&> repositories,
			const Serializers::SerializerInterface& serializer,
			const Services::IncomingMessageProcessor& incomingMessageProcessor,
			Transports::TransportInterface& transport
		) :
			m_Repositories(repositories),
			m_Serializer(serializer),
			m_IncomingMessageProcessor(incomingMessageProcessor),
			m_Transport(transport)
		{

		}

		void Start()
		{
			for (const auto& kvp : m_Repositories)
			{
				kvp.second.Init();
			}

			m_ConnectingThread = std::thread(&WorldHandler::Connect, this);
			m_SendingThread = std::thread(&WorldHandler::Send, this);
			m_ReceivingThread = std::thread(&WorldHandler::Receive, this);

			m_ExcludeThreadIds.insert(GetThreadId(m_SendingThread.native_handle()));
			m_ExcludeThreadIds.insert(GetThreadId(m_ConnectingThread.native_handle()));
			m_ExcludeThreadIds.insert(GetThreadId(m_ReceivingThread.native_handle()));
		}

		void Stop()
		{
			m_Stopped = true;
			if (m_ConnectingThread.joinable())
			{
				m_ConnectingThread.join();
			}
			if (m_SendingThread.joinable())
			{
				m_SendingThread.join();
			}
			if (m_ReceivingThread.joinable())
			{
				m_ReceivingThread.join();
			}
		}

		virtual ~WorldHandler() = default;

	private:
		void Send()
		{
			static int sendTick = 0;
			while (!m_Stopped)
			{
				try {
					++sendTick;

					// If Client just connected, force a full entity dump
					// by resetting hash cache IN the Send thread (no race).
					if (m_NeedFullDump.load())
					{
						m_OutgoingMessageBuilder.Reset();
						m_NeedFullDump.store(false);
					}

					const auto& messages = GetOutgoingMessages();

					// Execute queued game commands (Move, Attack, etc.)
					std::vector<std::function<void()>> commands;
					{
						std::lock_guard<std::mutex> cmdLock(Interlude::g_CommandMutex);
						Interlude::g_CommandQueue.swap(commands);
					}
					for (const auto& cmd : commands) {
						cmd();
					}

					if (m_Transport.IsConnected())
					{
						for (const auto& message : messages)
						{
							const auto serialized = m_Serializer.Serialize(message);
							m_Transport.Send(serialized);
						}
					}

					// Write status log every 20 ticks
					if (sendTick % 20 == 0) {
						FILE* f = nullptr;
						errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\bot_status.log", L"a");
						if (err == 0 && f) {
							fprintf(f, "[STATUS] tick=%d msgs=%d cmds=%d connected=%d\n",
								sendTick, (int)messages.size(), (int)commands.size(),
								m_Transport.IsConnected() ? 1 : 0);
							fflush(f); fclose(f);
						}
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				// Client disconnect causes pipe errors — NOT fatal.
				// The pipe code sets IsConnected=false, Connect thread
				// will wait for a new Client. Do NOT set m_Stopped=true.
				catch (const CriticalRuntimeException& e)
				{
					// Only truly fatal if we're NOT connected anymore
					// (pipe error = client gone, recoverable)
					if (!m_Transport.IsConnected()) {
						ServiceLocator::GetInstance().GetLogger()->Warning(
							L"Send: pipe error (client disconnected?): " + e.Message());
						// Don't stop — wait for reconnect
					} else {
						// Still connected but got critical error — truly fatal
						m_Stopped = true;
						ServiceLocator::GetInstance().GetLogger()->Error(e.Message());
					}
				}
				catch (const RuntimeException& e)
				{
					ServiceLocator::GetInstance().GetLogger()->Warning(e.Message());
				}
				catch (...)
				{
				}
			}
		}

		void Receive()
		{
			while (!m_Stopped)
			{
				try {
					if (m_Transport.IsConnected())
					{
						const auto& message = m_Transport.Receive();
						if (!message.empty()) {
							const auto messageType = m_IncomingMessageProcessor.Process(message);

							if (messageType == Serializers::IncomingMessage::Type::invalidate) {
								Invalidate();
							}
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
				// Client disconnect causes ERROR_BROKEN_PIPE — NOT fatal.
				// The pipe code sets IsConnected=false, Connect thread
				// will accept a new Client connection.
				catch (const CriticalRuntimeException& e)
				{
					if (!m_Transport.IsConnected()) {
						ServiceLocator::GetInstance().GetLogger()->Warning(
							L"Receive: pipe error (client disconnected?): " + e.Message());
						// Don't stop — wait for reconnect
					} else {
						m_Stopped = true;
						ServiceLocator::GetInstance().GetLogger()->Error(e.Message());
					}
				}
				catch (const RuntimeException& e)
				{
					ServiceLocator::GetInstance().GetLogger()->Warning(e.Message());
				}
			}
		}

		void Connect()
		{
			while (!m_Stopped)
			{
				try {
					if (!m_Transport.IsConnected())
					{
						m_Transport.Connect();
						// Signal Send thread to do a full entity dump
						// on its next iteration. We set the flag here
						// but the actual Reset() happens in Send thread
						// to avoid race condition with GetOutgoingMessages().
						m_NeedFullDump.store(true);
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
				catch (const CriticalRuntimeException& e)
				{
					if (!m_Transport.IsConnected()) {
						// Connection failed — not fatal, try again
						ServiceLocator::GetInstance().GetLogger()->Warning(
							L"Connect: failed (will retry): " + e.Message());
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
					} else {
						m_Stopped = true;
						ServiceLocator::GetInstance().GetLogger()->Error(e.Message());
					}
				}
				catch (const RuntimeException& e)
				{
					ServiceLocator::GetInstance().GetLogger()->Warning(e.Message());
				}
			}
		}

		const std::vector<std::vector<Serializers::Node>> GetOutgoingMessages()
		{
			std::vector<std::vector<Serializers::Node>> result;

			for (const auto& kvp : m_Repositories)
			{
				try {
					auto& entities = kvp.second.GetEntities();
					const auto& messages = m_OutgoingMessageBuilder.Build(kvp.first, entities);
					result.insert(result.end(), messages.begin(), messages.end());
				} catch (const std::exception&) {
				} catch (...) {
				}
			}

			return result;
		}

		void Invalidate()
		{
			for (const auto& kvp : m_Repositories)
			{
				kvp.second.Reset();
			}
			m_OutgoingMessageBuilder.Reset();
		}

	private:
		const std::map<std::wstring, Repositories::EntityRepositoryInterface&> m_Repositories;
		const Serializers::SerializerInterface& m_Serializer;
		const Services::IncomingMessageProcessor m_IncomingMessageProcessor;
		Services::OutgoingMessageBuilder m_OutgoingMessageBuilder;

		Transports::TransportInterface& m_Transport;
		volatile bool m_Stopped = false;
		std::atomic<bool> m_NeedFullDump{false};
		std::thread m_ConnectingThread;
		std::thread m_SendingThread;
		std::thread m_ReceivingThread;
		std::set<DWORD> m_ExcludeThreadIds;
	};

}
