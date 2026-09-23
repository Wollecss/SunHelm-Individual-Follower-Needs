#include "Feeding.h"

#include "EAS.h"
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
	// Every outcome is logged, including the ones where nothing happens. A raw meal with no line
	// after it used to be indistinguishable between four different reasons - the risk switched off
	// in SunHelm, an immune race, a lucky roll, or the spell failing to resolve - which is exactly
	// the silence AGENTS.md warns about. The roll result is worth having in the log too, because a
	// probability bug is invisible in any single session.
	void ApplyRawFoodRisk(const Followers::State& a_state, RE::Actor& a_actor)
	{
		if (!SunHelm::RawFoodDamageEnabled()) {
			logger::info("{} ate raw food; no risk (SunHelm's raw food damage is off)", a_state.name);
			return;
		}
		if (SunHelm::IsImmuneToFoodPoisoning(&a_actor)) {
			logger::info("{} ate raw food; immune to food poisoning", a_state.name);
			return;
		}

		static std::mt19937 engine{ std::random_device{}() };
		// 0-99 so "< 30" is exactly 30%. An inclusive 0-100 would have been 30/101.
		static std::uniform_int_distribution<int> roll{ 0, 99 };

		// SunHelm's fallback when diseases are switched off: a flat health bite instead of illness.
		if (!SunHelm::DiseasesEnabled()) {
			static std::uniform_int_distribution<int> damage{ 0, 25 };
			const auto                                dealt = damage(engine);
			a_actor.AsActorValueOwner()->ModActorValue(
				RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -static_cast<float>(dealt));
			logger::info("{} took {} damage from raw food (diseases are off in SunHelm)",
				a_state.name, dealt);
			return;
		}

		constexpr int kPoisoningChance = 30;
		const auto    rolled = roll(engine);
		if (rolled >= kPoisoningChance) {
			logger::info("{} ate raw food and got away with it (rolled {}, needed under {})",
				a_state.name, rolled, kPoisoningChance);
			return;
		}

		auto* poisoning = SunHelm::FoodPoisoningSpell();
		if (!poisoning) {
			logger::error("{} should have caught food poisoning (rolled {}) but the spell did not "
						  "resolve - illness from raw food is not working",
				a_state.name, rolled);
			return;
		}
		if (a_actor.HasSpell(poisoning)) {
			logger::info("{} ate raw food while already ill (rolled {})", a_state.name, rolled);
			return;
		}
		a_actor.AddSpell(poisoning);
		logger::info("{} caught food poisoning from raw food (rolled {})", a_state.name, rolled);
		if (Settings::Get().notifyConsumption) {
			RE::DebugNotification(std::format("{} looks unwell.", a_state.name).c_str());
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

	// Returns whether an animation was played, so the caller's log line can say. Without that,
	// "ate bread" reads identically whether EAS animated it, didn't cover it, or never resolved -
	// and the first in-game test of this feature had no way to tell those apart from the log.
	bool Consume(RE::Actor& a_actor, const Candidate& a_candidate)
	{
		// EquipObject on a potion/food item is the same native call the engine makes for any
		// actor "using" one - it applies the item's effect and removes it from inventory itself,
		// the same as a player quick-using food, so no manual RemoveItem is needed.
		//
		// It does NOT produce an animation on its own. Eating Animations and Sounds only watches
		// the player equip something, so nothing reacts when a follower does. The animation is
		// asked for separately below, in the same order EAS does it - equip first, then cast - so
		// a follower eating looks the same as the player eating.
		RE::ActorEquipManager::GetSingleton()->EquipObject(&a_actor, a_candidate.object);

		return Settings::Get().animateConsumption && EAS::Play(a_actor, a_candidate.object);
	}

	// Only says anything when EAS is installed: with it absent the line logged at startup already
	// explains the silence, and tagging every meal "no animation" would be noise. With it present,
	// "not covered" is the useful answer - it means the item is outside EAS's list, not that this
	// is broken.
	const char* AnimationNote(bool a_animated)
	{
		if (!EAS::IsAvailable()) {
			return "";
		}
		return a_animated ? " [animated]" : " [no EAS animation for this item]";
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
			const auto animated = Consume(a_actor, candidate);
			a_state.lastConsumedHours[0] = nowHours;
			logger::info("{} ate{} '{}' (-{:.0f} hunger, from {:.1f}){}", a_state.name,
				candidate.kind == SunHelm::FoodKind::kRaw ? " raw" : "", ItemLabel(candidate.object),
				candidate.restore, a_state.hunger, AnimationNote(animated));
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

	// Thirst is the usual reason to drink, but not the only one. A follower in a tavern will finish
	// a pint they bought for the company rather than the thirst - without this, the social drink
	// bought one every half hour and never touched it, stockpiling ale while the drink counter sat
	// still. Being drunk doesn't stop them: they carry on like anyone else at the bar, and the
	// evening ends when they leave rather than when they hit a number. What paces it is the
	// consumption cooldown and the fact that they only drink what they've bought.
	const auto thirstyEnough =
		SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst) >= settings.drinkAtStage;
	const auto sociallyDrinking =
		settings.buyAlcohol && settings.drunkEffects && SunHelm::IsInInn(&a_actor);

	if (settings.trackThirst && SunHelm::IsNeedEnabled(SunHelm::Need::kThirst) &&
		CooldownElapsed(a_state, 1, nowHours) && (thirstyEnough || sociallyDrinking)) {
		// When it's only sociability driving this, ale is the point - water would satisfy nothing.
		static constexpr std::array kDrinkKinds{ SunHelm::FoodKind::kDrink,
			SunHelm::FoodKind::kWaterskin, SunHelm::FoodKind::kAlcohol };
		static constexpr std::array kSocialKinds{ SunHelm::FoodKind::kAlcohol };
		const auto candidate = thirstyEnough
		                           ? BestCandidate(a_actor, SunHelm::ThirstRestore, kDrinkKinds)
		                           : BestCandidate(a_actor, SunHelm::ThirstRestore, kSocialKinds);

		if (candidate.object) {
			const auto animated = Consume(a_actor, candidate);
			a_state.lastConsumedHours[1] = nowHours;
			if (candidate.kind == SunHelm::FoodKind::kAlcohol) {
				++a_state.drinksHad;
				a_state.lastDrinkHours = nowHours;
			}
			logger::info("{} drank '{}' (-{:.0f} thirst, from {:.1f}){}", a_state.name,
				ItemLabel(candidate.object), candidate.restore, a_state.thirst,
				AnimationNote(animated));
			a_state.thirst = std::clamp(
				a_state.thirst - candidate.restore, 0.0f, SunHelm::MaxLevel(SunHelm::Need::kThirst));

			a_state.outOfSupplyNotifiedStage[1] = -1;
			if (settings.notifyConsumption) {
				RE::DebugNotification(
					std::format("{} drinks {}.", a_state.name, ItemLabel(candidate.object)).c_str());
			}
		} else if (thirstyEnough) {
			// Only worth saying when thirst actually drove this. A follower who merely fancied a
			// pint and hasn't got one is not out of supplies, and saying so would be a lie that
			// also buries the real warnings.
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
