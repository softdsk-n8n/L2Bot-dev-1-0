#pragma once

#include <shared_mutex>
#include "Domain/Repositories/EntityRepositoryInterface.h"
#include "../Factories/HeroFactory.h"
#include "Domain/Events/HeroCreatedEvent.h"
#include "Domain/Events/HeroDeletedEvent.h"
#include "Domain/Events/CreatureDiedEvent.h"
#include "Domain/Events/AttackedEvent.h"
#include "../GameStructs/NetworkHandlerWrapper.h"
#include "Domain/Services/ServiceLocator.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	class HeroRepository : public Repositories::EntityRepositoryInterface
	{
	public:
		HeroRepository(NetworkHandlerWrapper& networkHandler, HeroFactory& factory) :
			m_NetworkHandler(networkHandler),
			m_Factory(factory)
		{
		}
		HeroRepository() = delete;
		virtual ~HeroRepository() = default;

	public:
		const std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> GetEntities() override
		{
			std::unique_lock<std::shared_timed_mutex> lock(m_Mutex);

		const auto hero = m_NetworkHandler.GetHero();

		std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> result;
			if (hero) {
				if (!m_Hero) {
					try {
						m_Hero = m_Factory.Create(hero);
						if (m_Hero) {
							Services::ServiceLocator::GetInstance().GetEventDispatcher()->Dispatch(Events::HeroCreatedEvent{});
							Services::ServiceLocator::GetInstance().GetLogger()->App(L"%s enter in the world", m_Hero->GetFullName().GetNickname().c_str());
						}
					} catch (const RuntimeException&) {
						m_Hero = nullptr;
					} catch (const std::exception&) {
						m_Hero = nullptr;
					} catch (...) {
						m_Hero = nullptr;
					}
				}
				else
				{
					try {
						m_Factory.Update(m_Hero, hero);
					} catch (const RuntimeException&) {
						m_Hero = nullptr;
						return result;
					} catch (const std::exception&) {
						m_Hero = nullptr;
						return result;
					} catch (...) {
						Services::ServiceLocator::GetInstance().GetLogger()->Info(L"HeroFactory Update failed");
						m_Hero = nullptr;
						return result;
					}
					const auto attackers = std::map<uint32_t, uint32_t>(m_Hero->GetAttackerIds());
					for (const auto kvp : attackers)
					{
						const auto attacker = m_NetworkHandler.GetUser(kvp.first);
						// try to remove creature out of sight from the attackers
						if (attacker == nullptr)
						{
							m_Hero->RemoveAttacker(kvp.first);
						}
					}
				}
				result[hero->objectId] = m_Hero;
			}
			else if (m_Hero) {
				Services::ServiceLocator::GetInstance().GetLogger()->App(L"%s leave the world", m_Hero->GetFullName().GetNickname().c_str());
				m_Hero = nullptr;
				Services::ServiceLocator::GetInstance().GetEventDispatcher()->Dispatch(Events::HeroDeletedEvent{});
			}

			return result;
		}

		void Reset() override
		{
			std::unique_lock<std::shared_timed_mutex> lock(m_Mutex);
			m_Hero = nullptr;
		}

		void Init() override
		{
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Subscribe(Events::CreatureDiedEvent::name, [this](const Events::Event& evt) {
				OnCreatureDied(evt);
			});
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Subscribe(Events::AttackedEvent::name, [this](const Events::Event& evt) {
				OnAttacked(evt);
			});
		}

		void OnCreatureDied(const Events::Event& evt)
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			if (evt.GetName() == Events::CreatureDiedEvent::name)
			{
				const auto casted = static_cast<const Events::CreatureDiedEvent&>(evt);
				if (m_Hero)
				{
					if (m_Hero->GetId() == casted.GetCreatureId())
					{
						Services::ServiceLocator::GetInstance().GetLogger()->App(L"%s died", m_Hero->GetFullName().GetNickname().c_str());
						m_Hero->ClearAttackers();
					}
					else
					{
						// try to remove dead creature from the attackers
						m_Hero->RemoveAttacker(casted.GetCreatureId());
					}
				}
			}
		}

		void OnAttacked(const Events::Event& evt)
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			if (evt.GetName() == Events::AttackedEvent::name)
			{
				const auto casted = static_cast<const Events::AttackedEvent&>(evt);
				if (m_Hero)
				{
					if (casted.GetAttackerId() == m_Hero->GetId())
					{
						// hero is attacked
					}
					else if (casted.GetTargetId() == m_Hero->GetId())
					{
						m_Hero->AddAttacker(casted.GetAttackerId());
					}
				}
			}
		}

	private:
		NetworkHandlerWrapper& m_NetworkHandler;
		HeroFactory m_Factory;
		std::shared_ptr<Entities::Hero> m_Hero;
		std::shared_timed_mutex m_Mutex;
	};
}
