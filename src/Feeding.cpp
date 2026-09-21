#include "Feeding.h"

#include "Settings.h"
#include "SunHelm.h"

namespace
{
	struct Candidate
	{
		RE::TESBoundObject* object{ nullptr };
		SunHelm::FoodKind   kind{ SunHelm::FoodKind::kNone };
		float               restore{ 0.0f };
	};

	// Stops a Ravenous follower emptying their pack in seconds: consumption is attempted every tick,
	// so without this four meals could land inside a minute, which both contradicts relief only
	// "taking the edge off" and burns through supplies the player provided.
	bool CooldownElapsed(const Followers::State& a_state, std::size_t a_slot, float a_nowHours)
	{
		const auto since = a_nowHours - a_state.lastConsumedHours[a_slot];
		// A negative gap means the clock went backwards (an older save was loaded), so treat the
		// cooldown as spent rather than trusting the number.
		return since < 0.0f || since >= Settings::Get().consumeCooldownHours;
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
		std::span<const SunHelm::FoodKind> a_wanted)
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

	// SunHelm's own Ravenous threshold - the point at which a follower stops being picky.
	constexpr int kDesperateStage = 4;

	// Mirrors what _SHEatDetection does to the player for raw food: with diseases on it's a 30%
	// chance of food poisoning, and with them off it's a flat bite of health instead.
	void ApplyRawFoodRisk(const Followers::State& a_state, RE::Actor& a_actor)
	{
		if (!SunHelm::RawFoodDamageEnabled() || SunHelm::IsImmuneToFoodPoisoning(&a_actor)) {
			return;
		}

		static std::mt19937                       engine{ std::random_device{}() };
		static std::uniform_int_distribution<int> roll{ 0, 100 };

		if (!SunHelm::DiseasesEnabled()) {
			static std::uniform_int_distribution<int> damage{ 0, 25 };
			a_actor.AsActorValueOwner()->RestoreActorValue(
				RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -static_cast<float>(damage(engine)));
			return;
		}

		if (roll(engine) >= 30) {
			return;
		}

		auto* poisoning = SunHelm::FoodPoisoningSpell();
		if (poisoning && !a_actor.HasSpell(poisoning)) {
			a_actor.AddSpell(poisoning);
			logger::info("{} caught food poisoning from raw food", a_state.name);
			if (Settings::Get().notifyConsumption) {
				RE::DebugNotification(
					std::format("{} looks unwell.", a_state.name).c_str());
			}
		}
	}

	// Drinking a cure potion they're carrying. Deliberately not on the consumption cooldown: being
	// ill isn't a craving, and making someone stay sick because they ate recently would be odd.
	bool TryCureSelf(const Followers::State& a_state, RE::Actor& a_actor)
	{
		if (!Settings::Get().selfCureWithPotions || !SunHelm::IsDiseased(&a_actor)) {
			return false;
		}

		for (const auto& [object, entry] : a_actor.GetInventory()) {
			if (!object || entry.first <= 0 || !SunHelm::IsCureDiseasePotion(object)) {
				continue;
			}
			RE::ActorEquipManager::GetSingleton()->EquipObject(&a_actor, object);
			const auto cured = SunHelm::CureDiseases(&a_actor);
			logger::info("{} drank a cure potion ({} ailment(s) cured)", a_state.name, cured);
			if (Settings::Get().notifyConsumption) {
				RE::DebugNotification(std::format("{} looks better.", a_state.name).c_str());
			}
			return true;
		}
		return false;
	}

	void Consume(RE::Actor& a_actor, const Candidate& a_candidate)
	{
		// EquipObject on a potion/food item is the same native call the engine makes for any
		// actor "using" one - it applies the item's effect and removes it from inventory itself,
		// the same as a player quick-using food. No manual RemoveItem or animation call needed.
		RE::ActorEquipManager::GetSingleton()->EquipObject(&a_actor, a_candidate.object);
	}

	using SunHelm::ItemLabel;
}

void Feeding::TryEatAndDrink(Followers::State& a_state, RE::Actor& a_actor)
{
	const auto& settings = Settings::Get();
	if (!settings.allowSelfFeeding || !CanActRightNow(a_actor)) {
		return;
	}

	const auto nowHours = RE::Calendar::GetSingleton()->GetHoursPassed();

	// Before anything else: being ill is worth fixing ahead of being peckish.
	TryCureSelf(a_state, a_actor);

	const auto hungerStage = SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger);

	if (settings.trackHunger && SunHelm::IsNeedEnabled(SunHelm::Need::kHunger) &&
		CooldownElapsed(a_state, 0, nowHours) && hungerStage >= settings.eatAtStage) {
		auto wanted = std::vector{ SunHelm::FoodKind::kLight, SunHelm::FoodKind::kMedium,
			SunHelm::FoodKind::kHeavy, SunHelm::FoodKind::kSoup };

		// Raw meat is a last resort, not a meal: only once they're Ravenous, and only after the
		// proper food above has come up empty, since BestCandidate ranks on restore value and raw
		// food is worth nothing.
		const auto desperate = settings.eatRawWhenDesperate && hungerStage >= kDesperateStage;
		if (desperate) {
			wanted.push_back(SunHelm::FoodKind::kRaw);
		}

		const auto candidate = BestCandidate(a_actor, SunHelm::HungerRestore, wanted);

		if (candidate.object) {
			Consume(a_actor, candidate);
			a_state.lastConsumedHours[0] = nowHours;
			logger::info("{} ate{} '{}' (-{:.0f} hunger, from {:.1f})", a_state.name,
				candidate.kind == SunHelm::FoodKind::kRaw ? " raw" : "", ItemLabel(candidate.object),
				candidate.restore, a_state.hunger);
			if (candidate.kind == SunHelm::FoodKind::kRaw) {
				ApplyRawFoodRisk(a_state, a_actor);
			}
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
			if (a_state.outOfSupplyNotifiedStage[0] != stage) {
				a_state.outOfSupplyNotifiedStage[0] = stage;
				// Logged as well as announced. Without this, "found nothing edible" and "never
				// looked" are indistinguishable in the log - which is exactly how a bug that made
				// every drink unclassifiable stayed hidden for several sessions.
				logger::info("{} found nothing to eat (hunger stage {})", a_state.name, stage);
				if (settings.notifyNeed[static_cast<std::size_t>(SunHelm::Need::kHunger)]) {
					RE::DebugNotification(std::format("{} has nothing to eat.", a_state.name).c_str());
				}
			}
		}
	}

	if (settings.trackThirst && SunHelm::IsNeedEnabled(SunHelm::Need::kThirst) &&
		CooldownElapsed(a_state, 1, nowHours) &&
		SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst) >= settings.drinkAtStage) {
		static constexpr std::array kDrinkKinds{ SunHelm::FoodKind::kDrink,
			SunHelm::FoodKind::kWaterskin, SunHelm::FoodKind::kAlcohol };
		const auto candidate = BestCandidate(a_actor, SunHelm::ThirstRestore, kDrinkKinds);

		if (candidate.object) {
			Consume(a_actor, candidate);
			a_state.lastConsumedHours[1] = nowHours;
			if (candidate.kind == SunHelm::FoodKind::kAlcohol) {
				++a_state.drinksHad;
				a_state.lastDrinkHours = nowHours;
			}
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
			if (a_state.outOfSupplyNotifiedStage[1] != stage) {
				a_state.outOfSupplyNotifiedStage[1] = stage;
				logger::info("{} found nothing to drink (thirst stage {})", a_state.name, stage);
				if (settings.notifyNeed[static_cast<std::size_t>(SunHelm::Need::kThirst)]) {
					RE::DebugNotification(std::format("{} has nothing to drink.", a_state.name).c_str());
				}
			}
		}
	}
}
