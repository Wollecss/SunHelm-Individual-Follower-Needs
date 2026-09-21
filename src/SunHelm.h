#pragma once

// Integration layer over SunHelm Survival. Everything this plugin knows about SunHelm's forms,
// thresholds and restore amounts lives here so the rest of the code never hardcodes a FormID.
namespace SunHelm
{
	inline constexpr auto kPluginName = "SunHelmSurvival.esp"sv;

	enum class Need
	{
		kHunger = 0,
		kThirst,
		kFatigue,
		kCold,

		kTotal
	};

	inline constexpr std::array kAllNeeds{ Need::kHunger, Need::kThirst, Need::kFatigue, Need::kCold };

	// How a consumable is classified, mirroring the buckets _SHEatDetection/_SHDrinkDetection sort
	// items into. kSaltWater and kRaw exist to be excluded: SunHelm makes salt water *increase*
	// thirst and raw food risks food poisoning, so a follower should never pick either.
	enum class FoodKind
	{
		kNone = 0,
		kLight,
		kMedium,
		kHeavy,
		kSoup,
		kDrink,
		kAlcohol,
		kWaterskin,
		kSaltWater,
		kRaw,
		kIgnore
	};

	// Resolves SunHelm's forms. Call once data is loaded. Returns false when SunHelm isn't present.
	bool Resolve();
	bool IsAvailable();

	int              StageOf(Need a_need, float a_level);
	std::string_view StageLabel(Need a_need, int a_stage);
	std::string_view NeedName(Need a_need);

	// The level at which a need reaches the given stage, i.e. StageOf(need, StageFloor(need, s)) == s.
	float StageFloor(Need a_need, int a_stage);
	float MaxLevel(Need a_need);

	// Live reads of SunHelm's own globals, so followers always follow whatever the player has
	// configured in SunHelm's MCM rather than a duplicated setting of our own.
	float Rate(Need a_need);
	bool  IsNeedEnabled(Need a_need);
	float PlayerLevel(Need a_need);
	bool  IsModEnabled();
	bool  PausesInCombat();
	bool  PausesInDialogue();

	FoodKind Classify(RE::TESBoundObject* a_object);
	float    HungerRestore(FoodKind a_kind);
	float    ThirstRestore(FoodKind a_kind);

	// True while the actor is somewhere flagged as an inn - the same LocTypeInn check SunHelm uses
	// to decide whether to skip its own party food scaling.
	bool IsInInn(RE::Actor* a_actor);

	// An item of the given kind that can be bought at an inn, taken from SunHelm's own FormLists so
	// it's guaranteed to classify correctly and picks up anything a compatibility patch added.
	// Returns nullptr if nothing suitable resolved.
	RE::TESBoundObject* PickPurchasable(FoodKind a_kind);

	RE::TESBoundObject* Gold();

	// SunHelm's own drunk ability, and its MCM-configured number of drinks before it applies.
	RE::SpellItem* DrunkSpell();
	int            DrinksBeforeDrunk();

	// Disease handling is deliberately generic - matched on spell type and effect archetype rather
	// than on any particular mod's forms - so vanilla, Immersive Diseases and SunHelm's own
	// water-borne diseases are all recognised and cured without knowing anything about them.
	bool IsDiseased(RE::Actor* a_actor);
	int  CureDiseases(RE::Actor* a_actor);  // returns how many were removed
	bool IsCureDiseasePotion(RE::TESBoundObject* a_object);

	// SunHelm's own toggles for whether raw food carries a risk at all, and what kind.
	bool DiseasesEnabled();
	bool RawFoodDamageEnabled();

	// Mirrors SunHelm's ImmuneFoodPoisoning: beast races, Wood Elves and vampires shrug it off.
	bool IsImmuneToFoodPoisoning(RE::Actor* a_actor);
	RE::SpellItem* FoodPoisoningSpell();

	RE::SpellItem* StageSpell(Need a_need, int a_stage);

	// Which stage ability the actor is actually carrying for this need right now, or -1 for none.
	// Reads the actor's spell list rather than what we believe we applied, so it can catch the two
	// disagreeing.
	int AppliedStageOn(RE::Actor* a_actor, Need a_need);

	// Applies the stage's ability and removes the other five, so an actor only ever carries one
	// ability per need.
	void ApplyStageSpell(RE::Actor* a_actor, Need a_need, int a_stage);
	void ClearStageSpells(RE::Actor* a_actor, Need a_need);
}
