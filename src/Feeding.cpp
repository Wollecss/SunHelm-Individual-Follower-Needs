#include "Feeding.h"

#include "Settings.h"
#include "SunHelm.h"

namespace
{
	// Minimum game time between one follower's meals, and separately between their drinks. Without
	// it a Ravenous follower empties their pack in seconds: consumption is attempted every tick, so
	// four meals could land inside a minute, which both contradicts relief only "taking the edge
	// off" and burns through supplies the player provided. At a default timescale this is roughly
	// 45 real seconds.
	constexpr float kConsumeCooldownHours = 0.25f;

	struct Candidate
	{
		RE::TESBoundObject* object{ nullptr };
		SunHelm::FoodKind   kind{ SunHelm::FoodKind::kNone };
		float               restore{ 0.0f };
	};

	bool CooldownElapsed(const Followers::State& a_state, std::size_t a_slot, float a_nowHours)
	{
		const auto since = a_nowHours - a_state.lastConsumedHours[a_slot];
		// A negative gap means the clock went backwards (an older save was loaded), so treat the
		// cooldown as spent rather than trusting the number.
		return since < 0.0f || since >= kConsumeCooldownHours;
	}

	// Not in combat, actually present, and not the actor the player is currently talking to -
	// interrupting a conversation to eat a sandwich would be its own bug report. Being parked or
	// paused is already filtered by the caller before this ever runs.
	bool CanActRightNow(RE::Actor& a_actor)
	{
		if (a_actor.IsInCombat()) {
			return false;
		}
		if (auto* menu = RE::MenuTopicManager::GetSingleton()) {
			if (auto speaker = menu->speaker.get(); speaker && speaker->GetFormID() == a_actor.GetFormID()) {
				return false;
			}
		}
		return true;
	}

	// Highest restore value wins among what they're carrying. Ties keep whichever the inventory map
	// happened to iterate first - the map has no meaningful ordering to break ties by anyway.
	template <class RestoreFn>
	Candidate BestCandidate(RE::Actor& a_actor, RestoreFn a_restoreOf,
		std::initializer_list<SunHelm::FoodKind> a_wanted)
	{
		Candidate best;
		for (const auto& [object, entry] : a_actor.GetInventory()) {
			if (!object || entry.first <= 0) {
				continue;
			}
			const auto kind = SunHelm::Classify(object);
			if (std::ranges::find(a_wanted, kind) == a_wanted.end()) {
				continue;
			}
			if (const auto restore = a_restoreOf(kind); restore > best.restore) {
				best = { object, kind, restore };
			}
		}
		return best;
	}

	void Consume(RE::Actor& a_actor, const Candidate& a_candidate)
	{
		// EquipObject on a potion/food item is the same native call the engine makes for any
		// actor "using" one - it applies the item's effect and removes it from inventory itself,
		// the same as a player quick-using food. No manual RemoveItem or animation call needed.
		RE::ActorEquipManager::GetSingleton()->EquipObject(&a_actor, a_candidate.object);
	}

	std::string ItemLabel(RE::TESBoundObject* a_object)
	{
		if (auto* named = a_object->As<RE::TESFullName>(); named && named->GetFullName()[0] != '\0') {
			return named->GetFullName();
		}
		return "something";
	}
}

void Feeding::TryEatAndDrink(Followers::State& a_state, RE::Actor& a_actor)
{
	const auto& settings = Settings::Get();
	if (!settings.allowSelfFeeding || !CanActRightNow(a_actor)) {
		return;
	}

	const auto nowHours = RE::Calendar::GetSingleton()->GetHoursPassed();

	if (settings.trackHunger && SunHelm::IsNeedEnabled(SunHelm::Need::kHunger) &&
		CooldownElapsed(a_state, 0, nowHours) &&
		SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger) >= settings.eatAtStage) {
		const auto candidate = BestCandidate(a_actor, SunHelm::HungerRestore,
			{ SunHelm::FoodKind::kLight, SunHelm::FoodKind::kMedium, SunHelm::FoodKind::kHeavy,
				SunHelm::FoodKind::kSoup });

		if (candidate.object) {
			Consume(a_actor, candidate);
			a_state.lastConsumedHours[0] = nowHours;
			logger::info("{} ate '{}' (-{:.0f} hunger, from {:.1f})", a_state.name,
				ItemLabel(candidate.object), candidate.restore, a_state.hunger);
			a_state.hunger = std::clamp(
				a_state.hunger - candidate.restore, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kHunger));

			// Soup relieves thirst too, same as it does for the player in _SHEatDetection.
			if (candidate.kind == SunHelm::FoodKind::kSoup) {
				const auto soupThirst = SunHelm::ThirstRestore(SunHelm::FoodKind::kSoup);
				a_state.thirst = std::clamp(
					a_state.thirst - soupThirst, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kThirst));
			}

			a_state.outOfSupplyNotifiedStage[0] = -1;
			if (settings.notifyConsumption) {
				RE::DebugNotification(
					std::format("{} eats {}.", a_state.name, ItemLabel(candidate.object)).c_str());
			}
		} else {
			const auto stage = SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger);
			if (settings.notifyNeed[static_cast<std::size_t>(SunHelm::Need::kHunger)] &&
				a_state.outOfSupplyNotifiedStage[0] != stage) {
				a_state.outOfSupplyNotifiedStage[0] = stage;
				RE::DebugNotification(std::format("{} has nothing to eat.", a_state.name).c_str());
			}
		}
	}

	if (settings.trackThirst && SunHelm::IsNeedEnabled(SunHelm::Need::kThirst) &&
		CooldownElapsed(a_state, 1, nowHours) &&
		SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst) >= settings.drinkAtStage) {
		const auto candidate = BestCandidate(a_actor, SunHelm::ThirstRestore,
			{ SunHelm::FoodKind::kDrink, SunHelm::FoodKind::kWaterskin, SunHelm::FoodKind::kAlcohol });

		if (candidate.object) {
			Consume(a_actor, candidate);
			a_state.lastConsumedHours[1] = nowHours;
			logger::info("{} drank '{}' (-{:.0f} thirst, from {:.1f})", a_state.name,
				ItemLabel(candidate.object), candidate.restore, a_state.thirst);
			a_state.thirst = std::clamp(
				a_state.thirst - candidate.restore, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kThirst));

			a_state.outOfSupplyNotifiedStage[1] = -1;
			if (settings.notifyConsumption) {
				RE::DebugNotification(
					std::format("{} drinks {}.", a_state.name, ItemLabel(candidate.object)).c_str());
			}
		} else {
			const auto stage = SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst);
			if (settings.notifyNeed[static_cast<std::size_t>(SunHelm::Need::kThirst)] &&
				a_state.outOfSupplyNotifiedStage[1] != stage) {
				a_state.outOfSupplyNotifiedStage[1] = stage;
				RE::DebugNotification(std::format("{} has nothing to drink.", a_state.name).c_str());
			}
		}
	}
}
