#include "Tavern.h"

#include "Settings.h"
#include "SunHelm.h"

#include <random>

namespace
{
	// Matching the prices SunHelm charges the player at the same innkeepers: 20 for a hot meal and
	// 10 to fill a waterskin. Ale is priced off vanilla's own tavern rate.
	constexpr std::int32_t kMealPrice = 20;
	constexpr std::int32_t kWaterPrice = 10;
	constexpr std::int32_t kAlePrice = 5;

	enum Slot : std::size_t
	{
		kFoodSlot = 0,
		kDrinkSlot = 1,
		kAleSlot = 2
	};

	// Deliberately a longer leash than the consumption cooldown: buying is attempted every tick
	// while consumption is rate-limited, so without this a follower keeps buying while slowly
	// eating and empties their purse into a backpack full of bread.
	//
	// Ale runs on its own, shorter cooldown, because a round is not a meal.
	bool PurchaseCooldownElapsed(const Followers::State& a_state, std::size_t a_slot, float a_nowHours)
	{
		const auto& settings = Settings::Get();
		const auto  limit = a_slot == kAleSlot ? settings.aleCooldownHours : settings.purchaseCooldownHours;
		const auto  since = a_nowHours - a_state.lastPurchaseHours[a_slot];
		// A negative gap means an older save was loaded; treat the cooldown as spent rather than
		// trusting the clock.
		return since < 0.0f || since >= limit;
	}

	// Main thread only, so a plain static generator is fine.
	bool RollPercent(std::uint32_t a_chance)
	{
		static std::mt19937                            engine{ std::random_device{}() };
		static std::uniform_int_distribution<uint32_t> roll{ 0, 99 };
		return roll(engine) < a_chance;
	}

	using SunHelm::ItemLabel;

	// Takes the coin and hands over the goods. The follower eats or drinks it later, through the
	// same path they'd use for anything else in their pack.
	bool Buy(Followers::State& a_state, RE::Actor& a_actor, SunHelm::FoodKind a_kind,
		std::int32_t a_price, std::size_t a_slot, float a_nowHours)
	{
		auto* gold = SunHelm::Gold();
		auto* item = SunHelm::PickPurchasable(a_kind);
		if (!gold || !item) {
			return false;
		}

		a_actor.RemoveItem(gold, a_price, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		a_actor.AddObjectToContainer(item, nullptr, 1, nullptr);
		a_state.lastPurchaseHours[a_slot] = a_nowHours;

		logger::info("{} bought '{}' for {} gold ({} left)", a_state.name, ItemLabel(item), a_price,
			SunHelm::GoldAmount(&a_actor));
		if (Settings::Get().notifyConsumption) {
			RE::DebugNotification(
				std::format("{} buys {}.", a_state.name, ItemLabel(item)).c_str());
		}
		return true;
	}
}

bool Tavern::TryPurchase(Followers::State& a_state, RE::Actor& a_actor)
{
	const auto& settings = Settings::Get();
	if (!settings.buyAtInns || !SunHelm::IsInInn(&a_actor)) {
		return false;
	}

	const auto nowHours = RE::Calendar::GetSingleton()->GetHoursPassed();
	const auto gold = SunHelm::GoldAmount(&a_actor);

	const auto hungry = settings.trackHunger && SunHelm::IsNeedEnabled(SunHelm::Need::kHunger) &&
	                    SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger) >= settings.eatAtStage;
	const auto thirsty = settings.trackThirst && SunHelm::IsNeedEnabled(SunHelm::Need::kThirst) &&
	                     SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst) >= settings.drinkAtStage;

	if (hungry && PurchaseCooldownElapsed(a_state, kFoodSlot, nowHours)) {
		if (gold >= kMealPrice) {
			if (Buy(a_state, a_actor, SunHelm::FoodKind::kMedium, kMealPrice, kFoodSlot, nowHours)) {
				return true;
			}
		} else {
			// Said out loud, because "nothing happened" at an inn is otherwise indistinguishable
			// from the feature being broken - and an empty purse is the usual reason.
			logger::info("{} wanted a meal but has {} gold, needs {}", a_state.name, gold, kMealPrice);
			// Stamped on failure too, or the cooldown never starts and this re-runs every tick:
			// most vanilla followers carry no gold, so the common case was one line every five
			// seconds forever, burying the log this mod asks people to read. Waiting a full
			// purchase cooldown before trying again also stops pointless retries when nothing
			// about their purse has changed.
			a_state.lastPurchaseHours[kFoodSlot] = nowHours;
		}
	}

	const auto canBuyAle = settings.buyAlcohol && gold >= kAlePrice &&
	                       PurchaseCooldownElapsed(a_state, kAleSlot, nowHours);

	if (thirsty) {
		// What they'd order at the bar. Ale restores half what water does, so preferring it costs
		// them something real - which is the point. Rolled before the water branch rather than
		// used as a fallback, because "ale only when you can't afford water" meant a funded
		// follower in a tavern always drank water and effectively never got drunk.
		if (canBuyAle && RollPercent(static_cast<std::uint32_t>(settings.alePreferenceChance))) {
			if (Buy(a_state, a_actor, SunHelm::FoodKind::kAlcohol, kAlePrice, kAleSlot, nowHours)) {
				return true;
			}
		}

		if (PurchaseCooldownElapsed(a_state, kDrinkSlot, nowHours)) {
			if (gold >= kWaterPrice) {
				if (Buy(a_state, a_actor, SunHelm::FoodKind::kDrink, kWaterPrice, kDrinkSlot, nowHours)) {
					return true;
				}
			}
			// Still the cheap fallback when water is out of reach, ale roll or no ale roll.
			if (canBuyAle) {
				if (Buy(a_state, a_actor, SunHelm::FoodKind::kAlcohol, kAlePrice, kAleSlot, nowHours)) {
					return true;
				}
			}
			if (gold < kAlePrice) {
				logger::info("{} wanted a drink but has {} gold", a_state.name, gold);
				a_state.lastPurchaseHours[kDrinkSlot] = nowHours;  // See the meal branch above.
			}
		}
	}

	// A drink they don't strictly need, because they're in a tavern. Deliberately not gated on
	// being neither hungry nor thirsty any more: that rule said a thirsty follower was too thirsty
	// for a pint, which is backwards. It now simply means they've already dealt with what they
	// needed this round, or couldn't.
	//
	// Being drunk deliberately doesn't stop them. A follower who's had three keeps ordering like
	// anyone else in a tavern would, and since they drink what they buy nothing piles up. The
	// evening ends when they leave, not when they hit a counter.
	if (canBuyAle && RollPercent(static_cast<std::uint32_t>(settings.socialDrinkChance))) {
		return Buy(a_state, a_actor, SunHelm::FoodKind::kAlcohol, kAlePrice, kAleSlot, nowHours);
	}

	return false;
}
