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
		kDrinkSlot = 1
	};

	// Deliberately a longer leash than the consumption cooldown: buying is attempted every tick
	// while consumption is rate-limited, so without this a follower keeps buying while slowly
	// eating and empties their purse into a backpack full of bread.
	bool PurchaseCooldownElapsed(const Followers::State& a_state, std::size_t a_slot, float a_nowHours)
	{
		const auto since = a_nowHours - a_state.lastPurchaseHours[a_slot];
		// A negative gap means an older save was loaded; treat the cooldown as spent rather than
		// trusting the clock.
		return since < 0.0f || since >= Settings::Get().purchaseCooldownHours;
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
		}
	}

	if (thirsty && PurchaseCooldownElapsed(a_state, kDrinkSlot, nowHours)) {
		if (gold >= kWaterPrice) {
			if (Buy(a_state, a_actor, SunHelm::FoodKind::kDrink, kWaterPrice, kDrinkSlot, nowHours)) {
				return true;
			}
		}
		// Ale is the cheap fallback: SunHelm treats alcohol as worth half a drink for thirst, so
		// it's a worse answer than water, but it's what's affordable when the purse is nearly out.
		if (settings.buyAlcohol && gold >= kAlePrice) {
			if (Buy(a_state, a_actor, SunHelm::FoodKind::kAlcohol, kAlePrice, kDrinkSlot, nowHours)) {
				return true;
			}
		}
		if (gold < kAlePrice) {
			logger::info("{} wanted a drink but has {} gold", a_state.name, gold);
		}
	}

	// Nothing they need - but they're in a tavern with coin, so occasionally they just have a
	// drink. This is the only purchase that isn't need-driven.
	if (!hungry && !thirsty && settings.buyAlcohol && gold >= kAlePrice &&
		PurchaseCooldownElapsed(a_state, kDrinkSlot, nowHours) &&
		RollPercent(static_cast<std::uint32_t>(settings.socialDrinkChance))) {
		return Buy(a_state, a_actor, SunHelm::FoodKind::kAlcohol, kAlePrice, kDrinkSlot, nowHours);
	}

	return false;
}
