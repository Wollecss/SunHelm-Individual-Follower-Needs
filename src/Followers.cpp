#include "Followers.h"

#include "Needs.h"
#include "Settings.h"

#include <chrono>
#include <thread>

namespace
{
	struct PendingNeeds
	{
		float hunger{ 0.0f };
		float thirst{ 0.0f };
	};

	std::mutex                                       g_mutex;
	std::unordered_map<RE::FormID, Followers::State> g_tracked;

	// Needs read out of a save, waiting for their follower to be picked up again.
	std::unordered_map<RE::FormID, PendingNeeds> g_pendingRestore;

	std::atomic<bool> g_gameReady{ false };
	std::atomic<bool> g_running{ false };
	std::thread       g_pollThread;

	RE::BGSKeyword* g_actorTypeNPC{ nullptr };

	void ResolveActorTypeNPC()
	{
		if (g_actorTypeNPC) {
			return;
		}
		for (auto* keyword : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSKeyword>()) {
			if (!keyword) {
				continue;
			}
			if (const auto* editorID = keyword->GetFormEditorID(); editorID && "ActorTypeNPC"sv == editorID) {
				g_actorTypeNPC = keyword;
				return;
			}
		}
	}

	bool IsHumanoid(RE::Actor* a_actor)
	{
		// Creature companions are out of scope for now, so this filters on the race's ActorTypeNPC
		// keyword rather than trying to make SunHelm's food lists mean something for a wolf.
		if (!g_actorTypeNPC) {
			return true;
		}
		auto* race = a_actor->GetRace();
		return race && race->HasKeyword(g_actorTypeNPC);
	}

	bool IsFollower(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled()) {
			return false;
		}
		if (a_actor == RE::PlayerCharacter::GetSingleton()) {
			return false;
		}
		// IsPlayerTeammate covers every recruitment route that goes through vanilla follow
		// packages, which is what NFF builds on too - so this works the same with or without a
		// multi-follower framework, and needs no faction FormID of its own.
		if (!a_actor->IsPlayerTeammate()) {
			return false;
		}
		return IsHumanoid(a_actor);
	}

	// Parked or unloaded followers stop accumulating rather than quietly starving somewhere off
	// screen - they can't be fed while away, so letting them decay would only ever produce a
	// follower who is Starving the moment you collect them again.
	bool IsActive(RE::Actor* a_actor)
	{
		return a_actor->Is3DLoaded() &&
		       a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) != 1.0f;
	}

	void ClearAppliedSpells(RE::FormID a_formID)
	{
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
		if (!actor) {
			return;
		}
		for (const auto need : SunHelm::kAllNeeds) {
			SunHelm::ClearStageSpells(actor, need);
		}
	}

	void RefreshOnMainThread()
	{
		if (!g_gameReady.load() || !SunHelm::IsAvailable() || !Settings::Get().enabled) {
			return;
		}

		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return;
		}

		ResolveActorTypeNPC();

		// Built outside the lock: walking the process list touches the engine, so it stays on this
		// thread, but there's no reason to hold the state lock while doing it.
		std::vector<std::pair<RE::FormID, RE::Actor*>> present;
		for (const auto& handle : processLists->highActorHandles) {
			auto actor = handle.get();
			if (IsFollower(actor.get())) {
				present.emplace_back(actor->GetFormID(), actor.get());
			}
		}

		const auto maxTracked = std::clamp(Settings::Get().maxTracked, 1, Settings::kMaxSlots);

		std::vector<RE::FormID>                  dismissed;
		std::vector<std::pair<std::string, int>> hired;
		std::vector<std::string>                 restored;

		{
			std::lock_guard lock(g_mutex);

			for (auto it = g_tracked.begin(); it != g_tracked.end();) {
				const auto stillPresent = std::ranges::any_of(present, [&](const auto& a_entry) {
					return a_entry.first == it->first;
				});
				if (stillPresent) {
					++it;
					continue;
				}

				// A follower who merely walked out of high process is not dismissed - only one who
				// is loaded and no longer a teammate is. Anything not in the process list at all is
				// treated as gone, which is also what happens on dismissal.
				dismissed.push_back(it->first);
				it = g_tracked.erase(it);
			}

			for (const auto& [formID, actor] : present) {
				auto it = g_tracked.find(formID);
				if (it == g_tracked.end()) {
					if (std::cmp_greater_equal(g_tracked.size(), maxTracked)) {
						continue;
					}
					// Hiring starts everyone from rested and fed rather than trying to guess what
					// they were doing before they joined - unless this is the same follower coming
					// back from a loaded save, in which case their stored needs are restored.
					Followers::State state{};
					state.formID = formID;
					state.name = actor->GetDisplayFullName();
					state.lastTickHours = RE::Calendar::GetSingleton()->GetHoursPassed();
					state.active = IsActive(actor);

					if (const auto pending = g_pendingRestore.find(formID); pending != g_pendingRestore.end()) {
						state.hunger = pending->second.hunger;
						state.thirst = pending->second.thirst;
						g_pendingRestore.erase(pending);
						restored.push_back(state.name);
					} else {
						hired.emplace_back(state.name, static_cast<int>(g_tracked.size()) + 1);
					}

					g_tracked.emplace(formID, std::move(state));
				} else {
					it->second.active = IsActive(actor);
					if (it->second.name.empty()) {
						it->second.name = actor->GetDisplayFullName();
					}
				}
			}
		}

		for (const auto formID : dismissed) {
			ClearAppliedSpells(formID);
			logger::info("Stopped tracking follower {:08X}", formID);
		}
		for (const auto& [name, slot] : hired) {
			logger::info("Tracking follower '{}' in slot {}", name, slot);
		}
		for (const auto& name : restored) {
			logger::info("Restored tracked follower '{}' from the save", name);
		}
	}

	void PollLoop()
	{
		while (g_running.load()) {
			const auto interval = std::chrono::milliseconds(
				static_cast<int>(std::clamp(Settings::Get().pollSeconds, 1.0f, 60.0f) * 1000.0f));
			std::this_thread::sleep_for(interval);

			if (!g_running.load()) {
				return;
			}
			if (!g_gameReady.load()) {
				continue;
			}

			// Everything that reads the process list or an actor has to be on the main thread; the
			// game mutates both while this thread sleeps.
			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([]() {
					RefreshOnMainThread();
					Needs::Update();
				});
			}
		}
	}
}

void Followers::Start()
{
	if (g_running.exchange(true)) {
		return;
	}
	g_pollThread = std::thread(PollLoop);
	logger::info("Follower poll loop started");
}

void Followers::Stop()
{
	if (!g_running.exchange(false)) {
		return;
	}
	if (g_pollThread.joinable()) {
		g_pollThread.join();
	}
}

void Followers::SetGameReady(bool a_ready)
{
	g_gameReady.store(a_ready);
}

void Followers::Reset()
{
	std::lock_guard lock(g_mutex);
	g_tracked.clear();
	g_pendingRestore.clear();
}

void Followers::SeedRestoredNeeds(RE::FormID a_formID, float a_hunger, float a_thirst)
{
	std::lock_guard lock(g_mutex);
	g_pendingRestore[a_formID] = PendingNeeds{ a_hunger, a_thirst };
}

std::vector<Followers::State> Followers::Snapshot()
{
	std::lock_guard lock(g_mutex);
	std::vector<State> out;
	out.reserve(g_tracked.size());
	for (const auto& [formID, state] : g_tracked) {
		out.push_back(state);
	}
	std::ranges::sort(out, {}, &State::name);
	return out;
}

std::size_t Followers::TrackedCount()
{
	std::lock_guard lock(g_mutex);
	return g_tracked.size();
}

void Followers::ForEachTracked(const std::function<void(State&, RE::Actor&)>& a_visitor)
{
	std::lock_guard lock(g_mutex);
	for (auto& [formID, state] : g_tracked) {
		if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID)) {
			a_visitor(state, *actor);
		}
	}
}

namespace
{
	std::string ToLowerCopy(std::string_view a_text)
	{
		std::string out{ a_text };
		std::ranges::transform(out, out.begin(), [](unsigned char a_ch) {
			return static_cast<char>(std::tolower(a_ch));
		});
		return out;
	}
}

bool Followers::SetNeedsForTesting(std::string_view a_nameSubstring, std::optional<float> a_hunger,
	std::optional<float> a_thirst)
{
	const auto needle = ToLowerCopy(a_nameSubstring);

	std::lock_guard lock(g_mutex);
	for (auto& [formID, state] : g_tracked) {
		if (ToLowerCopy(state.name).find(needle) == std::string::npos) {
			continue;
		}
		if (a_hunger) {
			state.hunger = std::clamp(*a_hunger, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kHunger));
		}
		if (a_thirst) {
			state.thirst = std::clamp(*a_thirst, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kThirst));
		}
		return true;
	}
	return false;
}
