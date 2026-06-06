#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <tuple>
#include "../Entities/EntityInterface.h"
#include "../Enums/EntityStateEnum.h"
#include "../Serializers/Node.h"

namespace L2Bot::Domain::Services
{
	class OutgoingMessageBuilder
	{
	public:
		OutgoingMessageBuilder() = default;
		virtual ~OutgoingMessageBuilder() = default;

		void Reset()
		{
			m_Hashes.clear();
		}

		const std::vector <std::vector<Serializers::Node>> Build(const std::wstring& name, const std::unordered_map<uint32_t, std::shared_ptr<Entities::EntityInterface>>& entities)
		{
			std::vector <std::vector<Serializers::Node>> result;

			const auto& states = GetStates(name, entities);
			
			// Diagnostic: log when entities exist but no states generated (normal after initial dump)
			if (entities.size() > 0 && states.size() == 0) {
				static int diagCount = 0;
				if (++diagCount <= 3) {
					FILE* f = nullptr;
					errno_t err = _wfopen_s(&f, L"E:\\L2Teon\\system\\bot_status.log", L"a");
					if (err == 0 && f) {
						fwprintf(f, L"[BUILDER] repo=%s entities=%d states=0 hashEntries=%d (normal after initial dump)\n",
							name.c_str(), (int)entities.size(), (int)m_Hashes[name].size());
						fflush(f); fclose(f);
					}
				}
			}
			
			for (const auto& kvp : states) {
				const auto id = kvp.first;

				std::vector<Serializers::Node> message;

				std::wstring operation = L"none";
				switch (kvp.second)
				{
				case Enums::EntityStateEnum::created:
					operation = L"create";
					break;
				case Enums::EntityStateEnum::updated:
					operation = L"update";
					break;
				case Enums::EntityStateEnum::deleted:
					operation = L"delete";
				}

				message.push_back({ L"type", name });
				message.push_back({ L"operation", operation });
				if (entities.find(id) != entities.end()) {
					message.push_back({ L"content", entities.at(id)->BuildSerializationNodes() });
				}
				else {
					message.push_back({ L"content", {Serializers::Node{ L"id", std::to_wstring(id) }} });
				}

				result.push_back(message);
			}

			return result;
		}

	private:
		const std::unordered_map<uint32_t, Enums::EntityStateEnum> GetStates(const std::wstring& name, const std::unordered_map<uint32_t, std::shared_ptr<Entities::EntityInterface>>& entities)
		{
			std::unordered_map<uint32_t, Enums::EntityStateEnum> result;

			auto& hashes = m_Hashes[name];

			for (const auto& kvp : entities) {
				const auto& entity = kvp.second;

				if (hashes.size() == 0) {
					result[entity->GetId()] = Enums::EntityStateEnum::created;
				}
				else
				{
					if (hashes.find(entity->GetId()) == hashes.end()) {
						result[entity->GetId()] = Enums::EntityStateEnum::created;
					}
					else if (hashes[entity->GetId()] != entity->GetHash()) {
						result[entity->GetId()] = Enums::EntityStateEnum::updated;
					}
				}

				hashes[entity->GetId()] = entity->GetHash();
			}


			for (auto it = hashes.begin(); it != hashes.end();)
			{
				if (entities.find(it->first) == entities.end())
				{
					result[it->first] = Enums::EntityStateEnum::deleted;
					it = hashes.erase(it);
				}
				else
				{
					++it;
				}
			}

			return result;
		}

	private:
		std::unordered_map <std::wstring, std::unordered_map<uint32_t, size_t>> m_Hashes;
	};
}