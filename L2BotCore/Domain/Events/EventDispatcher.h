#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include "Event.h"

namespace L2Bot::Domain::Events
{
	class EventDispatcher
	{
	public:
		using Delegate = std::function<void(const Event&)>;
		using EventQueue = std::vector<std::unique_ptr<Event>>;

		EventDispatcher() = default;
		virtual ~EventDispatcher() = default;

		void Dispatch(const Event& evt)
		{
			const auto& name = evt.GetName();

			if (m_Handlers.find(name) == m_Handlers.end())
			{
				return;
			}

			for (const auto& handler : m_Handlers[name])
			{
				handler(evt);
			}
		}

		void Enqueue(const Event& evt)
		{
			m_Queue.push_back(std::unique_ptr<Event>(evt.Clone()));
		}

		void DispatchQueued()
		{
			auto queue = std::move(m_Queue);
			for (const auto& evt : queue)
			{
				Dispatch(*evt);
			}
		}

		// Drain the queue and return ownership - caller dispatches one-by-one
		EventQueue DrainQueue()
		{
			return std::move(m_Queue);
		}

		// Dispatch a single event
		void DispatchOne(const Event& evt)
		{
			Dispatch(evt);
		}

		void Subscribe(const std::string& eventName, const Delegate handler)
		{
			m_Handlers[eventName].push_back(handler);
		}

	private:
		std::unordered_map<std::string, std::vector<Delegate>> m_Handlers;
		EventQueue m_Queue;
	};
}
