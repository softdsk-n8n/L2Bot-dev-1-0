#include "pch.h"
#include "../../../Common/apihook.h"
#include "NetworkHandlerWrapper.h"
#include "GameEngineWrapper.h"
#include "Domain/Events/SpoiledEvent.h"
#include "ProcessManipulation.h"
#include "Domain/Services/ServiceLocator.h"
#include "Domain/Exceptions.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	extern bool IsPawnValid(APawn* pawn);
	extern bool ValidateAndReadPawnData(APawn* pawn, ALineagePlayerController*& outController, uint32_t& outTargetId, bool& outIsStanding);

	NetworkHandlerWrapper::NetworkHandler* NetworkHandlerWrapper::_target = 0;

	Item* (__thiscall* NetworkHandlerWrapper::__GetNextItem)(NetworkHandler*, float, int) = 0;
	User* (__thiscall* NetworkHandlerWrapper::__GetNextCreature)(NetworkHandler*, float, int) = 0;
	int(__thiscall* NetworkHandlerWrapper::__AddNetworkQueue)(NetworkHandler*, L2::NetworkPacket*) = 0;
	int(__thiscall* NetworkHandlerWrapper::__RequestItemList)(NetworkHandler*) = 0;
	User* (__thiscall* NetworkHandlerWrapper::__GetUser)(NetworkHandler*, int) = 0;
	Item* (__thiscall* NetworkHandlerWrapper::__GetItem)(NetworkHandler*, int) = 0;
	void(__thiscall* NetworkHandlerWrapper::__Action)(NetworkHandler*, int, L2::FVector, int) = 0;
	void(__thiscall* NetworkHandlerWrapper::__MTL)(NetworkHandler*, APawn*, L2::FVector, L2::FVector, void*, int) = 0;
	void(__thiscall* NetworkHandlerWrapper::__RequestMagicSkillUse)(NetworkHandler*, L2ParamStack&) = 0;
	int(__thiscall* NetworkHandlerWrapper::__RequestUseItem)(NetworkHandler*, L2ParamStack&) = 0;
	void(__thiscall* NetworkHandlerWrapper::__RequestAutoSoulShot)(NetworkHandler*, L2ParamStack&) = 0;
	void(__thiscall* NetworkHandlerWrapper::__ChangeWaitType)(NetworkHandler*, int) = 0;
	void(__thiscall* NetworkHandlerWrapper::__RequestRestartPoint)(NetworkHandler*, L2ParamStack&) = 0;

	// =====================================================================
	// Standalone SEH helpers — MUST be free functions (no C++ destructors
	// allowed in functions containing __try, per MSVC C2712).
	// __thiscall calling convention: this in ECX, args on stack.
	// We call via __fastcall cast: (ECX=this, EDX=dummy, stack=args).
	// =====================================================================

	static User* SafeCallGetNextCreature(void* target, void* fnPtr, float radius, int prevId)
	{
		__try {
			typedef User* (__fastcall *FnType)(void*, int, float, int);
			return ((FnType)fnPtr)(target, 0, radius, prevId);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return (User*)1; // 1 = crashed (distinguish from null = not found)
		}
	}

	static Item* SafeCallGetNextItem(void* target, void* fnPtr, float radius, int prevId)
	{
		__try {
			typedef Item* (__fastcall *FnType)(void*, int, float, int);
			return ((FnType)fnPtr)(target, 0, radius, prevId);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return (Item*)1;
		}
	}

	// =====================================================================
	// NetworkHandlerWrapper member functions
	// =====================================================================

	Item* NetworkHandlerWrapper::GetNextItem(float_t radius, int prevId) const
	{
		if (__GetNextItem && _target) {
			Item* result = SafeCallGetNextItem((void*)_target, (void*)__GetNextItem, radius, prevId);
			return (result == (Item*)1) ? 0 : result;
		}
		return 0;
	}

	User* NetworkHandlerWrapper::GetNextCreature(float_t radius, int prevId) const
	{
		if (__GetNextCreature && _target) {
			User* result = SafeCallGetNextCreature((void*)_target, (void*)__GetNextCreature, radius, prevId);
			return (result == (User*)1) ? 0 : result;
		}
		return 0;
	}

	User* NetworkHandlerWrapper::GetHero() const
	{
		if (!_target || !__GetNextCreature) {
			return 0;
		}

		// PRIMARY: Use game's own my_player_id global
		// Engine.dll+0x81F530 holds our character's objectId
		{
			int myId = GetMyObjectId();
			if (myId != 0) {
				User* hero = GetUser(myId);
				if (hero && hero->IsPlayer() && hero->pawn != nullptr) {
					return hero;
				}
			}
		}

		// FALLBACK: iterate creatures if direct lookup failed
		const auto creatures = FindAllObjects<User*>(4000.0f, [this](float_t radius, int32_t prevId) {
			return GetNextCreature(radius, prevId);
		});

		for (const auto& kvp : creatures)
		{
			const auto& creature = static_cast<User*>(kvp.second);
			if (creature->IsPlayer() && creature->pawn != nullptr && creature->classId > 0)
			{
				if (!IsPawnValid(creature->pawn)) {
					continue;
				}
				return creature;
			}
		}
		return 0;
	}

	int NetworkHandlerWrapper::GetMyObjectId() const
	{
		HMODULE hEng = GetModuleHandleA("Engine.dll");
		if (!hEng) return 0;
		__try {
			return *(int*)((char*)hEng + 0x81F530);
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			return 0;
		}
	}

	User* NetworkHandlerWrapper::GetUser(int objectId) const
	{
		__try {
			if (__GetUser && _target) {
				typedef User* (__fastcall *FnType)(void*, int, int);
				return ((FnType)(void*)__GetUser)((void*)_target, 0, objectId);
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}

	Item* NetworkHandlerWrapper::GetItem(int objectId) const
	{
		__try {
			if (__GetItem && _target) {
				typedef Item* (__fastcall *FnType)(void*, int, int);
				return ((FnType)(void*)__GetItem)((void*)_target, 0, objectId);
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}

	void NetworkHandlerWrapper::MTL(APawn* self, L2::FVector dst, L2::FVector src, void* terrainInfo, int unk1) const
	{
		__try {
			if (__MTL && _target) {
				typedef void (__fastcall *FnType)(void*, int, APawn*, L2::FVector, L2::FVector, void*, int);
				((FnType)(void*)__MTL)((void*)_target, 0, self, dst, src, terrainInfo, unk1);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	void NetworkHandlerWrapper::Action(int objectId, L2::FVector objectLocation, int unk) const
	{
		__try {
			if (__Action && _target) {
				typedef void (__fastcall *FnType)(void*, int, int, L2::FVector, int);
				((FnType)(void*)__Action)((void*)_target, 0, objectId, objectLocation, unk);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	void NetworkHandlerWrapper::RequestMagicSkillUse(L2ParamStack& stack) const
	{
		__try {
			if (__RequestMagicSkillUse && _target) {
				typedef void (__fastcall *FnType)(void*, int, L2ParamStack&);
				((FnType)(void*)__RequestMagicSkillUse)((void*)_target, 0, stack);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	int NetworkHandlerWrapper::RequestUseItem(L2ParamStack& stack) const
	{
		__try {
			if (__RequestUseItem && _target) {
				typedef int (__fastcall *FnType)(void*, int, L2ParamStack&);
				return ((FnType)(void*)__RequestUseItem)((void*)_target, 0, stack);
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}

	void NetworkHandlerWrapper::RequestAutoSoulShot(L2ParamStack& stack) const
	{
		__try {
			if (__RequestAutoSoulShot && _target) {
				typedef void (__fastcall *FnType)(void*, int, L2ParamStack&);
				((FnType)(void*)__RequestAutoSoulShot)((void*)_target, 0, stack);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	void NetworkHandlerWrapper::ChangeWaitType(int type) const
	{
		__try {
			if (__ChangeWaitType && _target) {
				typedef void (__fastcall *FnType)(void*, int, int);
				((FnType)(void*)__ChangeWaitType)((void*)_target, 0, type);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	int NetworkHandlerWrapper::RequestItemList() const
	{
		__try {
			if (__RequestItemList && _target) {
				typedef int (__fastcall *FnType)(void*, int);
				return ((FnType)(void*)__RequestItemList)((void*)_target, 0);
			}
			return 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}

	void NetworkHandlerWrapper::RequestRestartPoint(L2ParamStack& stack) const
	{
		__try {
			if (__RequestRestartPoint && _target) {
				typedef void (__fastcall *FnType)(void*, int, L2ParamStack&);
				((FnType)(void*)__RequestRestartPoint)((void*)_target, 0, stack);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// =====================================================================
	// Hook setup
	// =====================================================================

	static FARPROC ResolveThunk(FARPROC thunkAddr)
	{
		if (thunkAddr == 0) return 0;
		unsigned char* p = (unsigned char*)thunkAddr;
		if (p[0] == 0xE9) {
			int32_t offset = *(int32_t*)(p + 1);
			return (FARPROC)(p + 5 + offset);
		}
		return thunkAddr;
	}

	void NetworkHandlerWrapper::Init(HMODULE hModule)
	{
		(FARPROC&)__GetNextItem = GetProcAddress(hModule, "?GetNextItem@UNetworkHandler@@UAEPAUItem@@MH@Z");
		(FARPROC&)__GetNextCreature = GetProcAddress(hModule, "?GetNextCreature@UNetworkHandler@@UAEPAUUser@@MH@Z");
		(FARPROC&)__RequestItemList = GetProcAddress(hModule, "?RequestItemList@UNetworkHandler@@UAEHXZ");
		(FARPROC&)__GetUser = GetProcAddress(hModule, "?GetUser@UNetworkHandler@@UAEPAUUser@@H@Z");
		(FARPROC&)__GetItem = GetProcAddress(hModule, "?GetItem@UNetworkHandler@@UAEPAUItem@@H@Z");
		(FARPROC&)__MTL = GetProcAddress(hModule, "?MTL@UNetworkHandler@@UAEXPAVAActor@@VFVector@@10H@Z");
		(FARPROC&)__Action = GetProcAddress(hModule, "?Action@UNetworkHandler@@UAEXHVFVector@@H@Z");
		(FARPROC&)__RequestMagicSkillUse = GetProcAddress(hModule, "?RequestMagicSkillUse@UNetworkHandler@@UAEXAAVL2ParamStack@@@Z");
		(FARPROC&)__RequestUseItem = GetProcAddress(hModule, "?RequestUseItem@UNetworkHandler@@UAEHAAVL2ParamStack@@@Z");
		(FARPROC&)__RequestAutoSoulShot = GetProcAddress(hModule, "?RequestAutoSoulShot@UNetworkHandler@@UAEXAAVL2ParamStack@@@Z");
		(FARPROC&)__ChangeWaitType = GetProcAddress(hModule, "?ChangeWaitType@UNetworkHandler@@UAEXH@Z");
		(FARPROC&)__RequestRestartPoint = GetProcAddress(hModule, "?RequestRestartPoint@UNetworkHandler@@UAEXAAVL2ParamStack@@@Z");

		FARPROC addNQThunk = GetProcAddress(hModule, "?AddNetworkQueue@UNetworkHandler@@UAEHPAUNetworkPacket@@@Z");
		FARPROC addNQ = ResolveThunk(addNQThunk);
		if (addNQ) {
			(FARPROC&)__AddNetworkQueue = (FARPROC)splice(addNQ, __AddNetworkQueue_hook);
		}
	}

	void NetworkHandlerWrapper::Restore()
	{
		if (__AddNetworkQueue) restore((void*&)__AddNetworkQueue);
		Services::ServiceLocator::GetInstance().GetLogger()->Info(L"UNetworkHandler hooks restored");
	}

	int __fastcall NetworkHandlerWrapper::__AddNetworkQueue_hook(NetworkHandler* This, int, L2::NetworkPacket* packet)
	{
		if (_target == 0) {
			_target = This;
		}

		if (packet->id == static_cast<int>(L2::NetworkPacketId::SYSTEM_MESSAGE)) {
			L2::SystemMessagePacket* p = static_cast<L2::SystemMessagePacket*>(packet);
			if (
				p->GetMessageId() == static_cast<int>(L2::SystemMessagePacket::Type::SPOIL_SUCCESS) ||
				p->GetMessageId() == static_cast<int>(L2::SystemMessagePacket::Type::ALREADY_SPOILED)
				) {
				Services::ServiceLocator::GetInstance().GetEventDispatcher()->Enqueue(Events::SpoiledEvent{});
			}
		}

		return (*__AddNetworkQueue)(This, packet);
	}
}
