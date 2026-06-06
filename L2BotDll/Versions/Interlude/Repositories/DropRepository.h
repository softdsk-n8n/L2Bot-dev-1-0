#pragma once

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <cstdio>
#include "Domain/Repositories/EntityRepositoryInterface.h"
#include "Domain/DTO/EntityState.h"
#include "../Factories/DropFactory.h"
#include "../../GameStructs/FindObjectsTrait.h"
#include "../GameStructs/NetworkHandlerWrapper.h"
#include "../../GameStructs/GameStructs.h"

using namespace L2Bot::Domain;

namespace Interlude
{
	class DropRepository : public Repositories::EntityRepositoryInterface, public FindObjectsTrait
	{
	public:
		const std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> GetEntities() override
		{
			std::unique_lock<std::shared_timed_mutex> lock(m_Mutex);

			const auto allItems = FindAllObjects<Item*>(m_Radius, [this](float_t radius, int32_t prevId) {
				return m_NetworkHandler.GetNextItem(radius, prevId);
			});

			// Diagnostic: log how many items found, cache status
			static int dropTick = 0;
			++dropTick;
			if (dropTick % 50 == 0) {
				FILE* f = nullptr;
				errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\drop_debug.log", L"a");
				if (err == 0 && f) {
					fprintf(f, "[DROP_REPO] tick=%d items_found=%d cached_drops=%d cache_entries=%d\n",
						dropTick, (int)allItems.size(), (int)m_Drops.size(),
						(int)L2::DropPositionCache::Instance().Size());
					fflush(f); fclose(f);
				}
			}

			std::unordered_map<std::uint32_t, std::shared_ptr<Entities::EntityInterface>> result;
			int skippedCount = 0;
			for (const auto kvp : allItems) {
				const auto item = kvp.second;
				try {
					if (m_Drops.find(item->objectId) == m_Drops.end()) {
						m_Drops[item->objectId] = m_Factory.Create(item);
					}
					else
					{
						m_Factory.Update(m_Drops[item->objectId], item);
					}
					result[item->objectId] = m_Drops[item->objectId];
				} catch (const RuntimeException& e) {
					++skippedCount;
					// Log the specific exception for diagnosis
					if (dropTick % 50 == 0) {
						FILE* f = nullptr;
						errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\drop_debug.log", L"a");
						if (err == 0 && f) {
							fwprintf(f, L"[DROP_REPO] SKIP objId=%d itemId=%d hasPawn=%d err=%s\n",
								item->objectId, item->itemId, item->pawn ? 1 : 0, e.Message().c_str());
							fflush(f); fclose(f);
						}
					}
				} catch (...) {
					++skippedCount;
					if (dropTick % 50 == 0) {
						FILE* f = nullptr;
						errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\drop_debug.log", L"a");
						if (err == 0 && f) {
							fprintf(f, "[DROP_REPO] SKIP objId=%d itemId=%d hasPawn=%d err=unknown\n",
								item->objectId, item->itemId, item->pawn ? 1 : 0);
							fflush(f); fclose(f);
						}
					}
				}
			}

			// Log if we found items but all were skipped
			if (allItems.size() > 0 && result.size() == 0 && dropTick % 10 == 0) {
				FILE* f = nullptr;
				errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\drop_debug.log", L"a");
				if (err == 0 && f) {
					fprintf(f, "[DROP_REPO] WARNING: %d items found but %d skipped, 0 drops produced!\n",
						(int)allItems.size(), skippedCount);
					fflush(f); fclose(f);
				}
			}

			return result;
		}

		void Reset() override
		{
			std::shared_lock<std::shared_timed_mutex> lock(m_Mutex);
			m_Drops.clear();
		}

		DropRepository(const NetworkHandlerWrapper& networkHandler, const DropFactory& factory, const uint16_t radius) :
			m_NetworkHandler(networkHandler),
			m_Factory(factory),
			m_Radius(radius)
		{

		}

		DropRepository() = delete;
		virtual ~DropRepository() = default;

	private:
		const NetworkHandlerWrapper& m_NetworkHandler;
		const DropFactory& m_Factory;
		const uint16_t m_Radius;
		mutable std::shared_timed_mutex m_Mutex;
		std::unordered_map<uint32_t, std::shared_ptr<Entities::Drop>> m_Drops;
	};
}
