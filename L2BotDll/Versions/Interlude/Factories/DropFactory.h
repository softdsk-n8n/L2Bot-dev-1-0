#pragma once

#include <memory>
#include <format>
#include "../GameStructs/L2GameDataWrapper.h"
#include "../GameStructs/FName.h"
#include "../GameStructs/GameStructs.h"
#include "../../../Common/Common.h"
#include "Domain/Entities/Drop.h"
#include "Domain/Exceptions.h"

namespace Interlude
{
	class DropFactory
	{
	private:
		struct Data
		{
			uint32_t id;
			ValueObjects::Transform transform;
			uint32_t itemId;
			uint32_t amount;
			std::wstring name;
			std::wstring iconName;
		};

	public:
		DropFactory(const L2GameDataWrapper& l2GameData, const FName& fName) :
			m_L2GameData(l2GameData),
			m_FName(fName)
		{
		}

		DropFactory() = delete;
		virtual ~DropFactory() = default;

		std::shared_ptr<Entities::Drop> Create(const Item* item) const
		{
			const auto &data = GetData(item);

			return std::make_shared<Entities::Drop>(
				data.id,
				data.transform,
				data.itemId,
				data.amount,
				data.name,
				data.iconName
			);
		}

		void Update(std::shared_ptr<Entities::Drop>& drop, const Item* item) const
		{
			const auto& data = GetData(item);

			drop->Update(
				data.transform,
				data.itemId,
				data.amount,
				data.name,
				data.iconName
			);
		}

	private:
		const Data GetData(const Item* item) const
		{
			const auto itemData = m_L2GameData.GetItemData(item->itemId);
			// GetItemData may return null when _target not captured (Init hook timing).
			// Don't crash — empty name/icon is fine for pickup; only objectId+position matter.
			const auto nameEntry = itemData ? m_FName.GetEntry(itemData->nameIndex) : nullptr;
			const auto iconEntry = itemData ? m_FName.GetEntry(itemData->iconNameIndex) : nullptr;

			ValueObjects::Transform transform;
			
			if (item->pawn) {
				// Normal path: position from pawn->Location
				transform = ValueObjects::Transform(
					ValueObjects::Vector3(item->pawn->Location.x, item->pawn->Location.y, item->pawn->Location.z),
					ValueObjects::Vector3(
						static_cast<float_t>(item->pawn->Rotation.Pitch),
						static_cast<float_t>(item->pawn->Rotation.Yaw),
						static_cast<float_t>(item->pawn->Rotation.Roll)
					),
					ValueObjects::Vector3(item->pawn->Velocity.x, item->pawn->Velocity.y, item->pawn->Velocity.z),
					ValueObjects::Vector3(item->pawn->Acceleration.x, item->pawn->Acceleration.y, item->pawn->Acceleration.z)
				);
			} else {
				// Fallback: position from server packet (SpawnItem/DropItem)
				// This is how Walker gets drop positions — from the packet, not from memory.
				// Safer than reading memory, and works even when pawn is null.
				L2::FVector packetPos;
				if (L2::DropPositionCache::Instance().Retrieve(item->objectId, packetPos)) {
					transform = ValueObjects::Transform(
						ValueObjects::Vector3(packetPos.x, packetPos.y, packetPos.z),
						ValueObjects::Vector3(0, 0, 0),
						ValueObjects::Vector3(0, 0, 0),
						ValueObjects::Vector3(0, 0, 0)
					);
				} else {
					// No position available at all — skip this tick
					throw RuntimeException(std::format(L"drop {} has no pawn and no packet position", item->objectId));
				}
			}

			return {
				item->objectId,
				transform,
				item->itemId,
				item->amount,
				nameEntry ? std::wstring(nameEntry->value) : L"",
				iconEntry ? std::wstring(iconEntry->value) : L""
			};
		}

	private:
		const L2GameDataWrapper& m_L2GameData;
		const FName& m_FName;
	};
}
