#include "pch.h"
#include "../../../Common/apihook.h"
#include "../../../Common/Common.h"
#include "GameEngineWrapper.h"
#include "ProcessManipulation.h"
#include "Domain/Events/SkillCreatedEvent.h"
#include "Domain/Events/SkillUsedEvent.h"
#include "Domain/Events/SkillCancelledEvent.h"
#include "Domain/Events/AbnormalEffectChangedEvent.h"
#include "Domain/Events/ItemCreatedEvent.h"
#include "Domain/Events/ItemUpdatedEvent.h"
#include "Domain/Events/ItemDeletedEvent.h"
#include "Domain/Events/ItemAutousedEvent.h"
#include "Domain/Events/GameEngineTickedEvent.h"
#include "Domain/Events/ChatMessageCreatedEvent.h"
#include "Domain/Events/OnEndItemListEvent.h"
#include "Domain/Events/CreatureDiedEvent.h"
#include "Domain/Events/AttackedEvent.h"
#include "Domain/DTO/ItemData.h"
#include "Domain/DTO/ChatMessageData.h"
#include "FName.h"
#include "Domain/Services/ServiceLocator.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	// Standalone SEH wrapper: dispatch a single event safely.
	// Must be a C-style function (no C++ destructors) to allow __try.
	static void DispatchOneSafe(L2Bot::Domain::Events::EventDispatcher& dispatcher, const L2Bot::Domain::Events::Event& evt)
	{
		__try {
			dispatcher.DispatchOne(evt);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Skip this event - one bad handler won't kill the whole frame
		}
	}

	static void DispatchQueuedSafe()
	{
		auto& dispatcher = *Services::ServiceLocator::GetInstance().GetEventDispatcher();
		// Drain queue first (moves it out of the dispatcher), then dispatch each event
		// with per-event SEH so one crash doesn't skip the entire frame
		auto queue = dispatcher.DrainQueue();
		for (size_t i = 0; i < queue.size(); i++)
		{
			if (queue[i]) {
				DispatchOneSafe(dispatcher, *queue[i]);
			}
		}
	}

	bool IsPawnValid(APawn* pawn)
	{
		__try {
			float testX = pawn->Location.x;
			// Game coordinates are typically -300000 to 300000
			if (testX < -1000000.0f || testX > 1000000.0f || testX == 0.0f) {
				return false;
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false; // Access violation - pawn pointer is garbage
		}
	}

	bool ValidateAndReadPawnData(APawn* pawn, ALineagePlayerController*& outController, uint32_t& outTargetId, bool& outIsStanding)
	{
		__try {
			// Validate pawn Location
			float testX = pawn->Location.x;
			if (testX < -1000000.0f || testX > 1000000.0f || testX == 0.0f) {
				return false;
			}
			// Try to read playerController
			outController = pawn->lineagePlayerController;
			if (outController) {
				outTargetId = outController->targetObjectId == -1 ? 0 : (uint32_t)outController->targetObjectId;
				outIsStanding = outController->isStanding == 1;
			} else {
				outTargetId = 0;
				outIsStanding = true;
			}
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// pawn access or playerController access caused violation
			outController = nullptr;
			outTargetId = 0;
			outIsStanding = true;
			return false;
		}
	}

	GameEngineWrapper::GameEngine* GameEngineWrapper::_target = 0;

	void(__thiscall* GameEngineWrapper::__OnSkillListPacket)(GameEngine*, L2ParamStack&) = 0;
	int(__thiscall* GameEngineWrapper::__OnReceiveMagicSkillUse)(GameEngine*, User*, User*, L2ParamStack&) = 0;
	void(__thiscall* GameEngineWrapper::__OnReceiveMagicSkillCanceled)(GameEngine*, User*) = 0;
	void(__thiscall* GameEngineWrapper::__AddAbnormalStatus)(GameEngine*, L2ParamStack&) = 0;
	void(__thiscall* GameEngineWrapper::__AddInventoryItem)(GameEngine*, ItemInfo&) = 0;
	void(__thiscall* GameEngineWrapper::__OnReceiveUpdateItemList)(GameEngine*, UpdateItemListActionType, ItemInfo&) = 0;
	void(__thiscall* GameEngineWrapper::__OnExAutoSoulShot)(GameEngine*, L2ParamStack&) = 0;
	void(__thiscall* GameEngineWrapper::__OnSay2)(GameEngine*, L2ParamStack&) = 0;
	void(__thiscall* GameEngineWrapper::__OnEndItemList)(GameEngine*) = 0;
	float(__thiscall* GameEngineWrapper::__GetMaxTickRate)(GameEngine*) = 0;
	int(__thiscall* GameEngineWrapper::__OnDie)(GameEngine*, User*, L2ParamStack&) = 0;
	int(__thiscall* GameEngineWrapper::__OnAttack)(GameEngine*, User*, User*, int, int, int, int, int, L2::FVector, int) = 0;


	void GameEngineWrapper::Init(HMODULE hModule)
	{
		FARPROC proc = nullptr;

		proc = GetProcAddress(hModule, "?OnSkillListPacket@UGameEngine@@UAEXAAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__OnSkillListPacket = (FARPROC)splice(proc, __OnSkillListPacket_hook); }
		else { Services::ServiceLocator::GetInstance().GetLogger()->Info(L"OnSkillListPacket NOT FOUND in exports"); }

		proc = GetProcAddress(hModule, "?OnReceiveMagicSkillUse@UGameEngine@@UAEXPAUUser@@0AAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__OnReceiveMagicSkillUse = (FARPROC)splice(proc, __OnReceiveMagicSkillUse_hook); }

		proc = GetProcAddress(hModule, "?OnReceiveMagicSkillCanceled@UGameEngine@@UAEXPAUUser@@@Z");
		if (proc) { (FARPROC&)__OnReceiveMagicSkillCanceled = (FARPROC)splice(proc, __OnReceiveMagicSkillCanceled_hook); }

		proc = GetProcAddress(hModule, "?AddAbnormalStatus@UGameEngine@@UAEXAAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__AddAbnormalStatus = (FARPROC)splice(proc, __AddAbnormalStatus_hook); }

		proc = GetProcAddress(hModule, "?AddInventoryItem@UGameEngine@@UAEXAAUItemInfo@@@Z");
		if (proc) { (FARPROC&)__AddInventoryItem = (FARPROC)splice(proc, __AddInventoryItem_hook); }

		proc = GetProcAddress(hModule, "?OnReceiveUpdateItemList@UGameEngine@@UAEXHAAUItemInfo@@@Z");
		if (proc) { (FARPROC&)__OnReceiveUpdateItemList = (FARPROC)splice(proc, __OnReceiveUpdateItemList_hook); }

		proc = GetProcAddress(hModule, "?OnExAutoSoulShot@UGameEngine@@UAEXAAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__OnExAutoSoulShot = (FARPROC)splice(proc, __OnExAutoSoulShot_hook); }

		proc = GetProcAddress(hModule, "?OnSay2@UGameEngine@@UAEXAAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__OnSay2 = (FARPROC)splice(proc, __OnSay2_hook); }

		proc = GetProcAddress(hModule, "?OnEndItemList@UGameEngine@@UAEXXZ");
		if (proc) { (FARPROC&)__OnEndItemList = (FARPROC)splice(proc, __OnEndItemList_hook); }
		else { Services::ServiceLocator::GetInstance().GetLogger()->Info(L"OnEndItemList NOT FOUND in exports"); }

		proc = GetProcAddress(hModule, "?GetMaxTickRate@UGameEngine@@UAEMXZ");
		if (proc) { (FARPROC&)__GetMaxTickRate = (FARPROC)splice(proc, __GetMaxTickRate_hook); }

		proc = GetProcAddress(hModule, "?OnDie@UGameEngine@@UAEHPAUUser@@AAVL2ParamStack@@@Z");
		if (proc) { (FARPROC&)__OnDie = (FARPROC)splice(proc, __OnDie_hook); }
		else { Services::ServiceLocator::GetInstance().GetLogger()->Info(L"OnDie NOT FOUND in exports"); }

		proc = GetProcAddress(hModule, "?OnAttack@UGameEngine@@UAEHPAUUser@@0HHHHHVFVector@@H@Z");
		if (proc) { (FARPROC&)__OnAttack = (FARPROC)splice(proc, __OnAttack_hook); }
		else { Services::ServiceLocator::GetInstance().GetLogger()->Info(L"OnAttack NOT FOUND in exports"); }

		Services::ServiceLocator::GetInstance().GetLogger()->Info(L"UGameEngine hooks initialized (deferred dispatch)");
	}

	void GameEngineWrapper::Restore()
	{
		if (__OnSkillListPacket) restore((void*&)__OnSkillListPacket);
		if (__OnReceiveMagicSkillUse) restore((void*&)__OnReceiveMagicSkillUse);
		if (__OnReceiveMagicSkillCanceled) restore((void*&)__OnReceiveMagicSkillCanceled);
		if (__AddAbnormalStatus) restore((void*&)__AddAbnormalStatus);
		if (__AddInventoryItem) restore((void*&)__AddInventoryItem);
		if (__OnReceiveUpdateItemList) restore((void*&)__OnReceiveUpdateItemList);
		if (__OnExAutoSoulShot) restore((void*&)__OnExAutoSoulShot);
		if (__OnSay2) restore((void*&)__OnSay2);
		if (__OnEndItemList) restore((void*&)__OnEndItemList);
		if (__GetMaxTickRate) restore((void*&)__GetMaxTickRate);
		if (__OnDie) restore((void*&)__OnDie);
		if (__OnAttack) restore((void*&)__OnAttack);
		Services::ServiceLocator::GetInstance().GetLogger()->Info(L"UGameEngine hooks restored");
	}

	void __fastcall GameEngineWrapper::__OnSkillListPacket_hook(GameEngine* This, uint32_t, L2ParamStack& stack)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::SkillCreatedEvent{stack.GetBufferAsVector<int32_t>()});
		(*__OnSkillListPacket)(This, stack);
	}

	int __fastcall GameEngineWrapper::__OnReceiveMagicSkillUse_hook(GameEngine* This, uint32_t, User* attacker, User* target, L2ParamStack& stack)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::SkillUsedEvent{ stack.GetBufferAsVector<int32_t>() });
		if (attacker && target) {
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::AttackedEvent{ attacker->objectId, target->objectId });
		}
		return (*__OnReceiveMagicSkillUse)(This, attacker, target, stack);
	}

	void __fastcall GameEngineWrapper::__OnReceiveMagicSkillCanceled_hook(GameEngine* This, uint32_t, User* user)
	{
		if (user) {
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::SkillCancelledEvent{ user->objectId });
		}
		(*__OnReceiveMagicSkillCanceled)(This, user);
	}

	void __fastcall GameEngineWrapper::__AddAbnormalStatus_hook(GameEngine* This, uint32_t, L2ParamStack& stack)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::AbnormalEffectChangedEvent{ stack.GetBufferAsVector<int32_t>(3) });
		(*__AddAbnormalStatus)(This, stack);
	}

	void __fastcall GameEngineWrapper::__AddInventoryItem_hook(GameEngine* This, uint32_t, ItemInfo& itemInfo)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(
			Events::ItemCreatedEvent
			{
				DTO::ItemData
				{
					itemInfo.objectId,
					itemInfo.itemId,
					itemInfo.amount,
					itemInfo.isEquipped,
					itemInfo.enchantLevel,
					itemInfo.mana,
					itemInfo.type2 == L2::ItemType2::QUEST,
					itemInfo.itemSlot == L2::ItemSlot::LR_HAND
				}
			}
		);
		(*__AddInventoryItem)(This, itemInfo);
	}

	void __fastcall GameEngineWrapper::__OnReceiveUpdateItemList_hook(GameEngine* This, uint32_t, UpdateItemListActionType actionType, ItemInfo& itemInfo)
	{
		const DTO::ItemData itemData
		{
			itemInfo.objectId,
			itemInfo.itemId,
			itemInfo.amount,
			itemInfo.isEquipped,
			itemInfo.enchantLevel,
			itemInfo.mana,
			itemInfo.type2 == L2::ItemType2::QUEST,
			itemInfo.itemSlot == L2::ItemSlot::LR_HAND
		};

		switch (actionType)
		{
		case UpdateItemListActionType::created:
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::ItemCreatedEvent{ itemData });
			break;
		case UpdateItemListActionType::updated:
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::ItemUpdatedEvent{ itemData });
			break;
		case UpdateItemListActionType::deleted:
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::ItemDeletedEvent{ itemInfo.objectId });
			break;
		}
		(*__OnReceiveUpdateItemList)(This, actionType, itemInfo);
	}

	void __fastcall GameEngineWrapper::__OnExAutoSoulShot_hook(GameEngine* This, uint32_t, L2ParamStack& stack)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::ItemAutousedEvent{ stack.GetBufferAsVector<uint32_t>() });
		(*__OnExAutoSoulShot)(This, stack);
	}

	void __fastcall GameEngineWrapper::__OnSay2_hook(GameEngine* This, uint32_t, L2ParamStack& stack)
	{
		const auto buffer = stack.GetBufferAsVector<uint32_t>();

		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(
			Events::ChatMessageCreatedEvent
			{
				DTO::ChatMessageData
				{
					buffer[0],
					static_cast<uint8_t>(buffer[1]),
					std::wstring(reinterpret_cast<wchar_t*>(buffer[2])),
					std::wstring(reinterpret_cast<wchar_t*>(buffer[3]))
				}
			}
		);

		(*__OnSay2)(This, stack);
	}

	void __fastcall GameEngineWrapper::__OnEndItemList_hook(GameEngine* This, uint32_t)
	{
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::OnEndItemListEvent());
		(*__OnEndItemList)(This);
	}

	float __fastcall GameEngineWrapper::__GetMaxTickRate_hook(GameEngine* This, int)
	{
		static int hookCallCount = 0;
		if (++hookCallCount <= 3) {
			FILE* f = nullptr;
			errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\dll_debug.log", L"a");
			if (err == 0 && f) { fprintf(f, "[GetMaxTickRate] hook called count=%d This=0x%p\n", hookCallCount, This); fflush(f); fclose(f); }
		}

		if (_target == 0)
		{
			_target = This;
			Services::ServiceLocator::GetInstance().GetLogger()->Info(L"UGameEngine pointer 0x%08X obtained", (int)_target);
		}

		// Call original and return real FPS. Returning 0.0f caused division-by-zero in UE2.
		float fps = (*__GetMaxTickRate)(This);

		// SAFE POINT: GetMaxTickRate is called at the end of UGameEngine::Tick,
		// AFTER all packet processing is complete. Drain the deferred event queue here.
		DispatchQueuedSafe();

		// Enqueue the tick event too — it will be dispatched next frame
		Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::GameEngineTickedEvent{});

		return fps;
	}

	// Standalone SEH helper for OnDie/OnAttack — __try cannot coexist with C++ objects needing destructors
	// Read objectId from a potentially invalid User pointer
	static uint32_t ReadObjectIdSafe(User* user)
	{
		__try {
			return user ? user->objectId : 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
	}

	int __fastcall GameEngineWrapper::__OnDie_hook(GameEngine* This, int, User* creature, L2ParamStack& stack)
	{
		uint32_t objectId = ReadObjectIdSafe(creature);
		if (objectId != 0) {
			// NOTE: Do NOT read stack.GetBufferAsVector() — L2ParamStack is consumed/freed by
			// the original OnDie. Reading it before or after the call causes GPF.
			// Pass empty vector to CreatureDiedEvent.
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::CreatureDiedEvent{ objectId, {} });
		}
		return (*__OnDie)(This, creature, stack);
	}

	int __fastcall GameEngineWrapper::__OnAttack_hook(GameEngine* This, int, User* attacker, User* target, int unk0, int unk1, int unk2, int unk3, int unk4, L2::FVector unk5, int unk6)
	{
		uint32_t attackerId = ReadObjectIdSafe(attacker);
		uint32_t targetId = ReadObjectIdSafe(target);
		if (attackerId != 0 && targetId != 0) {
			Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::AttackedEvent{ attackerId, targetId });
		}

		return (*__OnAttack)(This, attacker, target, unk0, unk1, unk2, unk3, unk4, unk5, unk6);
	}
}
