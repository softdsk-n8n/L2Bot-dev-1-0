#pragma once

#include "Domain/Services/HeroServiceInterface.h"
#include "../Repositories/ItemRepository.h"
#include "../GameStructs/NetworkHandlerWrapper.h"
#include "../GameStructs/L2GameDataWrapper.h"
#include "Domain/Enums/RestartPointTypeEnum.h"
#include <functional>
#include <vector>
#include <mutex>

using namespace L2Bot::Domain;

namespace Interlude
{
	// Global command queue — Receive thread enqueues game commands,
	// Send thread executes them after StartCurrentProcess when L2
	// threads are running. This prevents crashes from calling MTL/Action
	// while L2 threads are frozen by StopCurrentProcess.
	extern std::mutex g_CommandMutex;
	extern std::vector<std::function<void()>> g_CommandQueue;

	class HeroService : public Services::HeroServiceInterface
	{
	public:
		HeroService(const NetworkHandlerWrapper& networkHandler, const ItemRepository& itemRespository, const L2GameDataWrapper& l2GameData) :
			m_NetworkHandler(networkHandler),
			m_ItemRespository(itemRespository),
			m_L2GameData(l2GameData)
		{

		}
		HeroService() = delete;
		virtual ~HeroService() = default;

		void Move(ValueObjects::Vector3 location) const override
		{
			auto hero = m_NetworkHandler.GetHero();
			
			if (hero && hero->pawn) {
				auto pawn = hero->pawn;
				L2::FVector dest = { location.GetX(), location.GetY(), location.GetZ() };
				L2::FVector src = pawn->Location;
				void* terrain = pawn->terrainInfo;
				auto& nh = m_NetworkHandler;

				std::lock_guard<std::mutex> lock(g_CommandMutex);
				g_CommandQueue.push_back([&nh, pawn, dest, src, terrain]() {
					nh.MTL(pawn, dest, src, terrain, 0);
				});
			}
		}

		void AcquireTarget(int objectId) const override
		{
			auto target = m_NetworkHandler.GetUser(objectId);

			if (target && target->pawn) {
				auto currentTargetId = 0;
				auto hero = m_NetworkHandler.GetHero();
				if (hero && hero->pawn && hero->pawn->lineagePlayerController) {
					currentTargetId = hero->pawn->lineagePlayerController->targetObjectId;
				}
				if (currentTargetId != objectId) {
					L2::FVector loc = target->pawn->Location;
					auto& nh = m_NetworkHandler;
					std::lock_guard<std::mutex> lock(g_CommandMutex);
					g_CommandQueue.push_back([&nh, objectId, loc]() {
						nh.Action(objectId, loc, 0);
					});
				}
			}
		}

		void Attack(int objectId) const override
		{
			auto target = m_NetworkHandler.GetUser(objectId);

			if (target && target->pawn) {
				L2::FVector loc = target->pawn->Location;
				auto& nh = m_NetworkHandler;
				std::lock_guard<std::mutex> lock(g_CommandMutex);
				g_CommandQueue.push_back([&nh, objectId, loc]() {
					nh.Action(objectId, loc, 0);
				});
				g_CommandQueue.push_back([&nh, objectId, loc]() {
					nh.Action(objectId, loc, 0);
				});
			}
		}

		void Pickup(int objectId) const override
		{
			// Walker-style pickup: Action packet sends HERO position, not drop position.
			// Server validates "is hero close enough to drop" using the origin coords.
			// Sending drop position is detectable as packet anomaly.
			auto hero = m_NetworkHandler.GetHero();
			if (hero && hero->pawn) {
				L2::FVector heroLoc = hero->pawn->Location;
				auto& nh = m_NetworkHandler;
				std::lock_guard<std::mutex> lock(g_CommandMutex);
				g_CommandQueue.push_back([&nh, objectId, heroLoc]() {
					nh.Action(objectId, heroLoc, 0);
				});
			}
		}

		void UseSkill(int skillId, bool isForced, bool isShiftPressed) const override
		{
			L2ParamStack* stack = new L2ParamStack(3);
			stack->PushBack((void*)skillId);
			stack->PushBack((void*)(isForced ? 1 : 0));
			stack->PushBack((void*)(isShiftPressed ? 1 : 0));

			m_NetworkHandler.RequestMagicSkillUse(*stack);

			delete stack;
		}

		void UseItem(int objectId) const override
		{
			L2ParamStack* stack = new L2ParamStack(1);
			stack->PushBack((void*)objectId);

			m_NetworkHandler.RequestUseItem(*stack);

			delete stack;
		}

		void ToggleAutouseSoulshot(int objectId) const override
		{
			const auto item = m_ItemRespository.GetItem(objectId);
			if (item)
			{
				const auto etcItem = static_cast<const Entities::EtcItem*>(item.get());

				L2ParamStack* stack = new L2ParamStack(2);
				stack->PushBack((void*)etcItem->GetItemId());
				stack->PushBack((void*)(etcItem->IsAutoused() ? 0 : 1));

				m_NetworkHandler.RequestAutoSoulShot(*stack);

				delete stack;
			}
		}

		void Sit() const override
		{
			m_NetworkHandler.ChangeWaitType(0);
		}

		void Stand() const override
		{
			m_NetworkHandler.ChangeWaitType(1);
		}

		void RestartPoint(Enums::RestartPointTypeEnum type) const override
		{
			L2ParamStack* stack = new L2ParamStack(1);
			stack->PushBack((void*)type);

			m_NetworkHandler.RequestRestartPoint(*stack);

			delete stack;
		}

	private:
		const NetworkHandlerWrapper& m_NetworkHandler;
		const ItemRepository& m_ItemRespository;
		const L2GameDataWrapper& m_L2GameData;
	};
}