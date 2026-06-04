#pragma once

#include <map>
#include <shared_mutex>
#include <Windows.h>
#include "../GameStructs/NetworkHandlerWrapper.h"
#include "Domain/Repositories/EntityRepositoryInterface.h"
#include "../Factories/NPCFactory.h"
#include "Domain/Events/SpoiledEvent.h"
#include "Domain/Events/CreatureDiedEvent.h"
#include "../../GameStructs/FindObjectsTrait.h"
#include "Domain/Services/ServiceLocator.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	class NPCRepository : public Repositories::EntityRepositoryInterface, public FindObjectsTrait
	{
	public:
		const std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> GetEntities() override
		{
			std::unique_lock<std::shared_timed_mutex> lock(m_Mutex);

			const auto allCreatures = FindAllObjects<User*>(m_Radius, [this](float_t radius, int32_t prevId) {
				return m_NetworkHandler.GetNextCreature(radius, prevId);
			});

			std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> result;
			for (const auto kvp : allCreatures) {
				const auto creature = kvp.second;
				// Teon fix: use IsNPC() (checks first 5 int32s) instead of broken userType field
				if (!creature->IsNPC() && !creature->IsMonster()) {
					continue;
				}

				try {
					if (m_Npcs.find(creature->objectId) == m_Npcs.end()) {
						m_Npcs[creature->objectId] = m_Factory.Create(creature);
					}
					else
					{
						m_Factory.Update(m_Npcs[creature->objectId], creature);
					}

					const auto spoilState = m_Spoiled.find(creature->objectId) == m_Spoiled.end() ? Enums::SpoilStateEnum::none : m_Spoiled[creature->objectId];
					m_Npcs[creature->objectId]->UpdateSpoilState(spoilState);

					result[creature->objectId] = m_Npcs[creature->objectId];
				} catch (...) {
					// Skip creatures with invalid data (no pawn, unreadable memory)
				}
			}

			return result;
		}

		void Reset() override
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			m_Npcs.clear();
		}

		void Init() override
		{
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Subscribe(Events::SpoiledEvent::name, [this](const Events::Event& evt) {
				OnSpoiled(evt);
			});
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Subscribe(Events::CreatureDiedEvent::name, [this](const Events::Event& evt) {
				OnCreatureDied(evt);
			});
		}

		NPCRepository(const NetworkHandlerWrapper& networkHandler, const NPCFactory& factory, const uint16_t radius) :
			m_NetworkHandler(networkHandler),
			m_Factory(factory),
			m_Radius(radius)
		{
		}

		NPCRepository() = delete;
		virtual ~NPCRepository() = default;

		void OnSpoiled(const Events::Event& evt)
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			if (evt.GetName() == Events::SpoiledEvent::name)
			{
				const auto casted = static_cast<const Events::SpoiledEvent&>(evt);
				const auto hero = m_NetworkHandler.GetHero();
				if (hero && hero->pawn && hero->pawn->lineagePlayerController)
				{
					MEMORY_BASIC_INFORMATION mbi;
					auto pc = hero->pawn->lineagePlayerController;
					if (VirtualQuery(pc, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
						!(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
						(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
						(size_t)pc + sizeof(int32_t) <= (size_t)mbi.BaseAddress + mbi.RegionSize)
					{
						const auto targetId = pc->targetObjectId;
						if (targetId)
						{
							m_Spoiled[targetId] = Enums::SpoilStateEnum::spoiled;
						}
					}
				}
			}
		}

		void OnCreatureDied(const Events::Event& evt)
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			if (evt.GetName() == Events::CreatureDiedEvent::name)
			{
				const auto casted = static_cast<const Events::CreatureDiedEvent&>(evt);
				if (m_Spoiled.find(casted.GetCreatureId()) != m_Spoiled.end())
				{
					const auto isSweepable = casted.GetCreatureInfo()[4] != 0;
					if (m_Spoiled[casted.GetCreatureId()] == Enums::SpoilStateEnum::spoiled && isSweepable)
					{
						m_Spoiled[casted.GetCreatureId()] = Enums::SpoilStateEnum::sweepable;
					}
					else
					{
						m_Spoiled[casted.GetCreatureId()] = Enums::SpoilStateEnum::none;
					}
				}
				if (m_Npcs.find(casted.GetCreatureId()) != m_Npcs.end()) {
					m_Npcs[casted.GetCreatureId()]->MarkAsDead();
				}
			}
		}

	private:
		const NPCFactory& m_Factory;
		std::map<uint32_t, Enums::SpoilStateEnum> m_Spoiled;
		const NetworkHandlerWrapper& m_NetworkHandler;
		const uint16_t m_Radius = 0;
		mutable std::shared_timed_mutex m_Mutex;
		std::unordered_map<uint32_t, std::shared_ptr<Entities::NPC>> m_Npcs;
	};
}
