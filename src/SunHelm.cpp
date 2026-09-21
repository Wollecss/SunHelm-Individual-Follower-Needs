#include "SunHelm.h"

namespace
{
	constexpr std::size_t kNeedCount = static_cast<std::size_t>(SunHelm::Need::kTotal);
	constexpr std::size_t kStageCount = 6;

	constexpr std::size_t Index(SunHelm::Need a_need) { return static_cast<std::size_t>(a_need); }

	// SunHelm's stage thresholds live as int properties on its quest scripts, not as
	// GlobalVariables, so they can't be read at runtime. These are transcribed from SunHelm's own
	// source (_SHHungerSystem, _SHThirstSystem, _SHFatigueSystem, _SHColdSystem) and match the
	// values SunHelm's existing CHIM bridge already hardcodes.
	constexpr std::array<std::array<float, kStageCount - 1>, kNeedCount> kThresholds{ {
		{ 40.0f, 80.0f, 140.0f, 240.0f, 360.0f },   // hunger
		{ 40.0f, 60.0f, 120.0f, 180.0f, 300.0f },   // thirst
		{ 80.0f, 160.0f, 240.0f, 360.0f, 480.0f },  // fatigue
		{ 25.0f, 150.0f, 250.0f, 450.0f, 700.0f },  // cold
	} };

	constexpr std::array<std::array<std::string_view, kStageCount>, kNeedCount> kStageLabels{ {
		{ "Well Fed"sv, "Satisfied"sv, "Peckish"sv, "Hungry"sv, "Ravenous"sv, "Starving"sv },
		{ "Quenched"sv, "Sated"sv, "Thirsty"sv, "Parched"sv, "Dehydrated"sv, "Severely Dehydrated"sv },
		{ "Well Rested"sv, "Rested"sv, "Slightly Tired"sv, "Tired"sv, "Weary"sv, "Exhausted"sv },
		{ "Warm"sv, "Comfortable"sv, "Chilly"sv, "Cold"sv, "Freezing"sv, "Frigid"sv },
	} };

	constexpr std::array<std::string_view, kNeedCount> kNeedNames{
		"Hunger"sv, "Thirst"sv, "Fatigue"sv, "Cold"sv
	};

	// Stage ability per need, verified against each spell's display name ("Hunger: Peckish" etc)
	// rather than inferred from FormID order - the fatigue set is not in FormID order.
	constexpr std::array<std::array<RE::FormID, kStageCount>, kNeedCount> kStageSpellIDs{ {
		{ 0x001824, 0x001825, 0x001827, 0x001829, 0x00182B, 0x00182D },  // hunger
		{ 0x01E850, 0x05C47E, 0x05C47F, 0x05C481, 0x05C483, 0x05C485 },  // thirst
		{ 0x01E846, 0x01E845, 0x01E848, 0x01E84A, 0x01E84C, 0x01E84E },  // fatigue
		{ 0x6E81DA, 0x6E81DB, 0x6E81DE, 0x6E81E0, 0x6E81E2, 0x6E81E4 },  // cold
	} };

	struct Forms
	{
		std::array<RE::TESGlobal*, kNeedCount> level{};
		std::array<RE::TESGlobal*, kNeedCount> rate{};
		std::array<RE::TESGlobal*, kNeedCount> disabled{};

		RE::TESGlobal* modEnabled{ nullptr };
		RE::TESGlobal* pauseCombat{ nullptr };
		RE::TESGlobal* pauseDialogue{ nullptr };
		RE::TESGlobal* numDrinks{ nullptr };

		RE::BGSKeyword*     locTypeInn{ nullptr };
		RE::BGSKeyword*     beastRace{ nullptr };
		RE::BGSKeyword*     vampire{ nullptr };
		RE::TESRace*        woodElf{ nullptr };
		RE::TESBoundObject* gold{ nullptr };
		RE::TESBoundObject* waterBottle{ nullptr };
		RE::SpellItem*      drunkSpell{ nullptr };
		RE::SpellItem*      foodPoisoning{ nullptr };
		RE::TESGlobal*      diseasesEnabled{ nullptr };
		RE::TESGlobal*      rawDamage{ nullptr };

		RE::BGSKeyword* lightFood{ nullptr };
		RE::BGSKeyword* mediumFood{ nullptr };
		RE::BGSKeyword* heavyFood{ nullptr };
		RE::BGSKeyword* soup{ nullptr };
		RE::BGSKeyword* drink{ nullptr };
		RE::BGSKeyword* alcohol{ nullptr };
		RE::BGSKeyword* saltWater{ nullptr };
		RE::BGSKeyword* foodIgnore{ nullptr };
		RE::BGSKeyword* meadWater{ nullptr };
		RE::BGSKeyword* wineWater{ nullptr };
		RE::BGSKeyword* sujammaWater{ nullptr };
		RE::BGSKeyword* rawFood{ nullptr };

		RE::BGSListForm* foodLightList{ nullptr };
		RE::BGSListForm* foodMediumList{ nullptr };
		RE::BGSListForm* foodHeavyList{ nullptr };
		RE::BGSListForm* soupList{ nullptr };
		RE::BGSListForm* drinkList{ nullptr };
		RE::BGSListForm* drinkNoBottleList{ nullptr };
		RE::BGSListForm* alcoholList{ nullptr };
		RE::BGSListForm* rawList{ nullptr };
		RE::BGSListForm* foodIgnoreList{ nullptr };
		RE::BGSListForm* waterskinList{ nullptr };

		std::array<std::array<RE::SpellItem*, kStageCount>, kNeedCount> stageSpells{};

		bool available{ false };
	};

	Forms g_forms;

	void ResolveGlobals()
	{
		// Globals keep their EditorID at runtime (TESGlobal stores it as a member), so they're
		// matched by name rather than FormID. That survives SunHelm renumbering its records.
		const std::unordered_map<std::string_view, RE::TESGlobal**> wanted{
			{ "_SHCurrentHungerLevel"sv, &g_forms.level[Index(SunHelm::Need::kHunger)] },
			{ "_SHCurrentThirstLevel"sv, &g_forms.level[Index(SunHelm::Need::kThirst)] },
			{ "_SHCurrentFatigueLevel"sv, &g_forms.level[Index(SunHelm::Need::kFatigue)] },
			{ "_SHCurrentColdLevel"sv, &g_forms.level[Index(SunHelm::Need::kCold)] },

			{ "_SHHungerRate"sv, &g_forms.rate[Index(SunHelm::Need::kHunger)] },
			{ "_SHThirstRate"sv, &g_forms.rate[Index(SunHelm::Need::kThirst)] },
			{ "_SHFatigueRate"sv, &g_forms.rate[Index(SunHelm::Need::kFatigue)] },
			{ "_SHRateGoal"sv, &g_forms.rate[Index(SunHelm::Need::kCold)] },

			{ "_SHHungerShouldBeDisabled"sv, &g_forms.disabled[Index(SunHelm::Need::kHunger)] },
			{ "_SHThirstShouldBeDisabled"sv, &g_forms.disabled[Index(SunHelm::Need::kThirst)] },
			{ "_SHFatigueShouldBeDisabled"sv, &g_forms.disabled[Index(SunHelm::Need::kFatigue)] },
			{ "_SHColdShouldBeDisabled"sv, &g_forms.disabled[Index(SunHelm::Need::kCold)] },

			{ "_SHEnabled"sv, &g_forms.modEnabled },
			{ "_SHPauseNeedsCombat"sv, &g_forms.pauseCombat },
			{ "_SHPauseNeedsDialogue"sv, &g_forms.pauseDialogue },
			{ "_SHNumDrinks"sv, &g_forms.numDrinks },
			{ "_SHDiseasesEnabled"sv, &g_forms.diseasesEnabled },
			{ "_SHRawDamage"sv, &g_forms.rawDamage },
		};

		for (auto* global : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESGlobal>()) {
			if (!global) {
				continue;
			}
			const auto* editorID = global->GetFormEditorID();
			if (!editorID) {
				continue;
			}
			if (const auto it = wanted.find(editorID); it != wanted.end()) {
				*it->second = global;
			}
		}
	}

	void ResolveKeywords()
	{
		// SunHelm injects its keywords into Update.esm rather than its own plugin, so matching by
		// EditorID avoids caring which file they actually came from. BGSKeyword keeps its EditorID.
		const std::unordered_map<std::string_view, RE::BGSKeyword**> wanted{
			{ "_SH_LightFoodKeyword"sv, &g_forms.lightFood },
			{ "_SH_MediumFoodKeyword"sv, &g_forms.mediumFood },
			{ "_SH_HeavyFoodKeyword"sv, &g_forms.heavyFood },
			{ "_SH_SoupKeyword"sv, &g_forms.soup },
			{ "_SH_DrinkKeyword"sv, &g_forms.drink },
			{ "_SH_AlcoholDrinkKeyword"sv, &g_forms.alcohol },
			{ "_SHSaltWaterKeyword"sv, &g_forms.saltWater },
			{ "_SHFoodIgnoreKeyword"sv, &g_forms.foodIgnore },
			{ "_SH_MeadWATERBottleKeyword"sv, &g_forms.meadWater },
			{ "_SH_WineWATERBottleKeyword"sv, &g_forms.wineWater },
			{ "_SH_SujammaWATERBottleKeyword"sv, &g_forms.sujammaWater },
			{ "VendorItemFoodRaw"sv, &g_forms.rawFood },
			// Vanilla, used to tell whether a follower is somewhere they could buy a meal.
			{ "LocTypeInn"sv, &g_forms.locTypeInn },
			// Vanilla, for SunHelm's food-poisoning immunity rules.
			{ "IsBeastRace"sv, &g_forms.beastRace },
			{ "Vampire"sv, &g_forms.vampire },
		};

		for (auto* keyword : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSKeyword>()) {
			if (!keyword) {
				continue;
			}
			const auto* editorID = keyword->GetFormEditorID();
			if (!editorID) {
				continue;
			}
			if (const auto it = wanted.find(editorID); it != wanted.end()) {
				*it->second = keyword;
			}
		}
	}

	void ResolveListsAndSpells()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();

		// FormLists and Spells don't keep an EditorID at runtime, so these have to go by FormID.
		// IDs were read out of SunHelmSurvival.esp directly rather than copied from anywhere.
		// A miss is logged rather than left silent: a null FormList quietly turns every membership
		// test into "no", which looks identical to an item simply not being food or drink.
		const auto list = [&](RE::FormID a_id, const char* a_name) {
			auto* found = handler->LookupForm<RE::BGSListForm>(a_id, SunHelm::kPluginName);
			if (!found) {
				logger::error("Could not resolve SunHelm FormList {} ({:06X})", a_name, a_id);
			}
			return found;
		};

		g_forms.foodLightList = list(0x01EDB5, "_SHFoodLightList");
		g_forms.foodMediumList = list(0x01EDB6, "_SHFoodMediumList");
		g_forms.foodHeavyList = list(0x01EDB7, "_SHFoodHeavyList");
		g_forms.soupList = list(0x029A62, "_SHSoupList");
		g_forms.drinkList = list(0x2A7941, "_SHDrinkList");
		g_forms.drinkNoBottleList = list(0x497B97, "_SHDrinkNoBottle");
		g_forms.alcoholList = list(0x33F759, "_SHAlcoholList");
		g_forms.rawList = list(0x1A04C7, "_SHRawList");
		g_forms.foodIgnoreList = list(0x09E19D, "_SHFoodIgnoreList");
		g_forms.waterskinList = list(0x4E3ABA, "_SHWaterskins");

		// Looked up as their concrete types, NOT as TESBoundObject. The templated lookups compare
		// against T::FORMTYPE, and TESBoundObject inherits FormType::None from TESForm - so asking
		// for one by that base type always returns null, silently.
		g_forms.gold = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000F);
		g_forms.waterBottle = handler->LookupForm<RE::AlchemyItem>(0x07AA96, SunHelm::kPluginName);
		g_forms.drunkSpell = handler->LookupForm<RE::SpellItem>(0x377265, SunHelm::kPluginName);
		g_forms.foodPoisoning = handler->LookupForm<RE::SpellItem>(0x6410BF, SunHelm::kPluginName);

		// Named individually: a lumped "one of these three failed" message costs a whole test cycle
		// to narrow down.
		const auto require = [](const void* a_form, const char* a_what) {
			if (!a_form) {
				logger::error("Could not resolve {}", a_what);
			}
		};
		require(g_forms.gold, "vanilla Gold001 - followers cannot buy anything");
		require(g_forms.waterBottle, "SunHelm's water bottle - followers cannot buy water");
		require(g_forms.drunkSpell, "SunHelm's drunk ability - followers cannot get drunk");
		require(g_forms.foodPoisoning, "SunHelm's food poisoning - raw food carries no risk");
		require(g_forms.locTypeInn, "vanilla LocTypeInn - inns cannot be detected");

		// Races keep their EditorID at runtime, so SunHelm's Wood Elf exception costs nothing.
		for (auto* race : handler->GetFormArray<RE::TESRace>()) {
			if (race) {
				if (const auto* editorID = race->GetFormEditorID(); editorID && "WoodElfRace"sv == editorID) {
					g_forms.woodElf = race;
					break;
				}
			}
		}
		int missingSpells = 0;
		for (std::size_t need = 0; need < kNeedCount; ++need) {
			for (std::size_t stage = 0; stage < kStageCount; ++stage) {
				auto* spell =
					handler->LookupForm<RE::SpellItem>(kStageSpellIDs[need][stage], SunHelm::kPluginName);
				if (!spell) {
					++missingSpells;
				}
				g_forms.stageSpells[need][stage] = spell;
			}
		}
		if (missingSpells > 0) {
			logger::error("{} of {} SunHelm stage abilities could not be resolved - follower "
						  "penalties will be incomplete",
				missingSpells, kNeedCount * kStageCount);
		}

		int missingKeywords = 0;
		for (auto* keyword : { g_forms.lightFood, g_forms.mediumFood, g_forms.heavyFood, g_forms.soup,
				 g_forms.drink, g_forms.alcohol, g_forms.saltWater, g_forms.foodIgnore,
				 g_forms.meadWater, g_forms.wineWater, g_forms.sujammaWater, g_forms.rawFood }) {
			if (!keyword) {
				++missingKeywords;
			}
		}
		if (missingKeywords > 0) {
			logger::error("{} food/drink keyword(s) could not be resolved - items will be "
						  "misclassified",
				missingKeywords);
		}
	}

	float ReadGlobal(RE::TESGlobal* a_global, float a_fallback)
	{
		return a_global ? a_global->value : a_fallback;
	}
}

bool SunHelm::Resolve()
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	// Checked against both load orders: SunHelm ships as a regular plugin, but someone compacting
	// it into an ESL shouldn't silently turn this whole mod off.
	const auto sunHelmLoaded = handler && (handler->LookupLoadedModByName(kPluginName) ||
											  handler->LookupLoadedLightModByName(kPluginName));
	if (!sunHelmLoaded) {
		logger::warn("SunHelmSurvival.esp is not loaded - follower needs will stay idle");
		g_forms.available = false;
		return false;
	}

	ResolveGlobals();
	ResolveKeywords();
	ResolveListsAndSpells();

	// The four level globals are the hard requirement: without them there is nothing to read or
	// mirror. Everything else degrades to "that feature is off" rather than failing outright.
	bool complete = true;
	for (const auto need : kAllNeeds) {
		if (!g_forms.level[Index(need)]) {
			logger::error("Could not resolve SunHelm's {} level global", NeedName(need));
			complete = false;
		}
	}

	g_forms.available = complete;
	if (complete) {
		// Deliberately no values here: kDataLoaded runs before save data is applied, so the globals
		// still hold SunHelmSurvival.esp's defaults at this point and logging them is misleading.
		logger::info("SunHelm resolved (forms found; values not read until the first tick)");
	}
	return complete;
}

bool SunHelm::IsAvailable()
{
	return g_forms.available;
}

int SunHelm::StageOf(Need a_need, float a_level)
{
	const auto& thresholds = kThresholds[Index(a_need)];
	for (std::size_t stage = 0; stage < thresholds.size(); ++stage) {
		if (a_level < thresholds[stage]) {
			return static_cast<int>(stage);
		}
	}
	return static_cast<int>(kStageCount) - 1;
}

std::string_view SunHelm::StageLabel(Need a_need, int a_stage)
{
	const auto stage = std::clamp<std::size_t>(static_cast<std::size_t>(a_stage), 0, kStageCount - 1);
	return kStageLabels[Index(a_need)][stage];
}

std::string_view SunHelm::NeedName(Need a_need)
{
	return kNeedNames[Index(a_need)];
}

float SunHelm::StageFloor(Need a_need, int a_stage)
{
	if (a_stage <= 0) {
		return 0.0f;
	}
	const auto& thresholds = kThresholds[Index(a_need)];
	const auto index = std::clamp<std::size_t>(static_cast<std::size_t>(a_stage) - 1, 0, thresholds.size() - 1);
	return thresholds[index];
}

float SunHelm::MaxLevel(Need a_need)
{
	const auto& thresholds = kThresholds[Index(a_need)];
	return thresholds.back();
}

float SunHelm::Rate(Need a_need)
{
	// Hunger/thirst/fatigue rates are "units gained per game hour" (SunHelm MCM default 10).
	// Cold's _SHRateGoal is a multiplier instead (default 1.0), which is why it isn't used to tick.
	return ReadGlobal(g_forms.rate[Index(a_need)], a_need == Need::kCold ? 1.0f : 10.0f);
}

bool SunHelm::IsNeedEnabled(Need a_need)
{
	return ReadGlobal(g_forms.disabled[Index(a_need)], 0.0f) == 0.0f;
}

float SunHelm::PlayerLevel(Need a_need)
{
	return ReadGlobal(g_forms.level[Index(a_need)], 0.0f);
}

bool SunHelm::IsModEnabled()
{
	return ReadGlobal(g_forms.modEnabled, 1.0f) != 0.0f;
}

bool SunHelm::PausesInCombat()
{
	return ReadGlobal(g_forms.pauseCombat, 1.0f) != 0.0f;
}

bool SunHelm::PausesInDialogue()
{
	return ReadGlobal(g_forms.pauseDialogue, 1.0f) != 0.0f;
}

SunHelm::FoodKind SunHelm::Classify(RE::TESBoundObject* a_object)
{
	if (!a_object || !g_forms.available) {
		return FoodKind::kNone;
	}

	auto* keyworded = a_object->As<RE::BGSKeywordForm>();
	const auto hasKeyword = [&](RE::BGSKeyword* a_keyword) {
		return keyworded && a_keyword && keyworded->HasKeyword(a_keyword);
	};
	const auto inList = [&](RE::BGSListForm* a_list) {
		return a_list && a_list->HasForm(a_object);
	};

	// Salt water first, and unconditionally: SunHelm makes it *raise* thirst, so it must never be
	// picked no matter what else it looks like.
	if (hasKeyword(g_forms.saltWater)) {
		return FoodKind::kSaltWater;
	}

	// Drinks are resolved BEFORE the food-ignore checks below, and that ordering is the whole
	// point. _SHFoodIgnoreKeyword and _SHFoodIgnoreList mean "don't categorise this as food" - not
	// "ignore this item". SunHelm's own _SHEatDetection deliberately files every drink there
	// (see CheckIgnoreCategorization) because drinks belong to _SHDrinkDetection instead, so the
	// list ships containing both water bottles and all three waterskins. Testing the ignore list
	// first therefore made every water source in the game unclassifiable, and followers would eat
	// happily but never drink.
	if (inList(g_forms.waterskinList)) {
		return FoodKind::kWaterskin;
	}
	if (hasKeyword(g_forms.alcohol) || inList(g_forms.alcoholList)) {
		return FoodKind::kAlcohol;
	}
	if (hasKeyword(g_forms.drink) || hasKeyword(g_forms.meadWater) || hasKeyword(g_forms.wineWater) ||
		hasKeyword(g_forms.sujammaWater) || inList(g_forms.drinkList) || inList(g_forms.drinkNoBottleList)) {
		return FoodKind::kDrink;
	}

	// Raw food can inflict food poisoning, so it's rejected rather than ranked.
	if (hasKeyword(g_forms.rawFood) || inList(g_forms.rawList)) {
		return FoodKind::kRaw;
	}

	// Anything still here that SunHelm has marked as not-food really isn't food.
	if (hasKeyword(g_forms.foodIgnore) || inList(g_forms.foodIgnoreList)) {
		return FoodKind::kIgnore;
	}

	// Soup is checked before the food tiers because it restores thirst and warmth on top of the
	// same amount a medium meal gives, so it must not be swallowed by the medium branch.
	if (hasKeyword(g_forms.soup) || inList(g_forms.soupList)) {
		return FoodKind::kSoup;
	}
	if (hasKeyword(g_forms.lightFood) || inList(g_forms.foodLightList)) {
		return FoodKind::kLight;
	}
	if (hasKeyword(g_forms.mediumFood) || inList(g_forms.foodMediumList)) {
		return FoodKind::kMedium;
	}
	if (hasKeyword(g_forms.heavyFood) || inList(g_forms.foodHeavyList)) {
		return FoodKind::kHeavy;
	}

	return FoodKind::kNone;
}

float SunHelm::HungerRestore(FoodKind a_kind)
{
	// Amounts taken from _SHEatDetection::FoodCheck so a follower's meal is worth exactly what the
	// same item is worth to the player.
	switch (a_kind) {
	case FoodKind::kLight:
		return 40.0f;
	case FoodKind::kMedium:
	case FoodKind::kSoup:
		return 75.0f;
	case FoodKind::kHeavy:
		return 125.0f;
	case FoodKind::kRaw:
		// Deliberately just under light food, so properly prepared food always wins the comparison
		// and raw meat is only ever reached when there's nothing else. SunHelm itself only gives
		// raw food a value when the item also happens to sit in one of its cooked-food lists, which
		// would leave a follower gnawing raw meat for no benefit at all - an irrational trade
		// against the poisoning risk, and not a decision worth modelling.
		return 35.0f;
	default:
		return 0.0f;
	}
}

float SunHelm::ThirstRestore(FoodKind a_kind)
{
	// _SHDrinkDetection uses a base of 80 for drinks and half that for alcohol; soup gives 20 on
	// top of its hunger value.
	switch (a_kind) {
	case FoodKind::kDrink:
	case FoodKind::kWaterskin:
		return 80.0f;
	case FoodKind::kAlcohol:
		return 40.0f;
	case FoodKind::kSoup:
		return 20.0f;
	default:
		return 0.0f;
	}
}

bool SunHelm::IsInInn(RE::Actor* a_actor)
{
	if (!a_actor || !g_forms.locTypeInn) {
		return false;
	}
	auto* location = a_actor->GetCurrentLocation();
	return location && location->HasKeyword(g_forms.locTypeInn);
}

RE::TESBoundObject* SunHelm::PickPurchasable(FoodKind a_kind)
{
	if (a_kind == FoodKind::kDrink || a_kind == FoodKind::kWaterskin) {
		return g_forms.waterBottle;
	}

	// Drawn from SunHelm's own lists rather than hardcoded vanilla FormIDs, so whatever an
	// innkeeper hands over is guaranteed to classify correctly on the way back in - including
	// anything a compatibility patch added to these lists.
	RE::BGSListForm* source = nullptr;
	switch (a_kind) {
	case FoodKind::kAlcohol:
		source = g_forms.alcoholList;
		break;
	case FoodKind::kLight:
		source = g_forms.foodLightList;
		break;
	case FoodKind::kHeavy:
		source = g_forms.foodHeavyList;
		break;
	default:
		source = g_forms.foodMediumList;
		break;
	}
	if (!source) {
		return nullptr;
	}

	// First entry that still resolves to something edible. List entries can reference forms from
	// plugins that aren't loaded, and Classify() is re-checked so a mis-filed entry can't produce
	// an item the follower would then refuse to consume.
	RE::TESBoundObject* found = nullptr;
	source->ForEachForm([&](RE::TESForm& a_form) {
		if (auto* object = a_form.As<RE::TESBoundObject>(); object && Classify(object) == a_kind) {
			found = object;
			return RE::BSContainer::ForEachResult::kStop;
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
	return found;
}

RE::TESBoundObject* SunHelm::Gold()
{
	return g_forms.gold;
}

RE::SpellItem* SunHelm::DrunkSpell()
{
	return g_forms.drunkSpell;
}

int SunHelm::DrinksBeforeDrunk()
{
	// SunHelm's MCM default is 3; clamped so a zero doesn't mean "drunk on an empty stomach".
	return std::max(1, static_cast<int>(std::lround(ReadGlobal(g_forms.numDrinks, 3.0f))));
}

namespace
{
	// Diseases are identified by spell type rather than by any particular mod's form list, so a
	// vanilla rockjoint, an Immersive Diseases NPC variant and SunHelm's own food poisoning are all
	// recognised without this plugin knowing they exist.
	bool IsDiseaseSpell(RE::SpellItem* a_spell)
	{
		return a_spell && a_spell->GetSpellType() == RE::MagicSystem::SpellType::kDisease;
	}
}

bool SunHelm::IsDiseased(RE::Actor* a_actor)
{
	if (!a_actor) {
		return false;
	}
	for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
		if (IsDiseaseSpell(spell)) {
			return true;
		}
	}
	return false;
}

int SunHelm::CureDiseases(RE::Actor* a_actor)
{
	if (!a_actor) {
		return 0;
	}

	// Collected first: removing while walking the actor's own spell list would invalidate it.
	std::vector<RE::SpellItem*> diseases;
	for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
		if (IsDiseaseSpell(spell)) {
			diseases.push_back(spell);
		}
	}
	for (auto* disease : diseases) {
		a_actor->RemoveSpell(disease);
	}
	return static_cast<int>(diseases.size());
}

bool SunHelm::IsCureDiseasePotion(RE::TESBoundObject* a_object)
{
	// Matched on the effect archetype, so any mod's cure potion works, not just vanilla's.
	auto* potion = a_object ? a_object->As<RE::AlchemyItem>() : nullptr;
	if (!potion) {
		return false;
	}
	for (auto* effect : potion->effects) {
		if (effect && effect->baseEffect &&
			effect->baseEffect->HasArchetype(RE::EffectSetting::Archetype::kCureDisease)) {
			return true;
		}
	}
	return false;
}

bool SunHelm::DiseasesEnabled()
{
	return ReadGlobal(g_forms.diseasesEnabled, 1.0f) != 0.0f;
}

bool SunHelm::RawFoodDamageEnabled()
{
	return ReadGlobal(g_forms.rawDamage, 1.0f) != 0.0f;
}

bool SunHelm::IsImmuneToFoodPoisoning(RE::Actor* a_actor)
{
	if (!a_actor) {
		return true;
	}
	auto* race = a_actor->GetRace();
	if (!race) {
		return false;
	}
	// SunHelm also exempts werewolves, but that's read from its own player-only state, so it has no
	// follower equivalent to check.
	if (g_forms.beastRace && race->HasKeyword(g_forms.beastRace)) {
		return true;
	}
	if (g_forms.woodElf && race == g_forms.woodElf) {
		return true;
	}
	if (g_forms.vampire && race->HasKeyword(g_forms.vampire)) {
		return true;
	}
	return false;
}

RE::SpellItem* SunHelm::FoodPoisoningSpell()
{
	return g_forms.foodPoisoning;
}

RE::SpellItem* SunHelm::StageSpell(Need a_need, int a_stage)
{
	if (a_stage < 0 || a_stage >= static_cast<int>(kStageCount)) {
		return nullptr;
	}
	return g_forms.stageSpells[Index(a_need)][static_cast<std::size_t>(a_stage)];
}

int SunHelm::AppliedStageOn(RE::Actor* a_actor, Need a_need)
{
	if (!a_actor) {
		return -1;
	}
	for (std::size_t stage = 0; stage < kStageCount; ++stage) {
		auto* spell = g_forms.stageSpells[Index(a_need)][stage];
		if (spell && a_actor->HasSpell(spell)) {
			return static_cast<int>(stage);
		}
	}
	return -1;
}

void SunHelm::ApplyStageSpell(RE::Actor* a_actor, Need a_need, int a_stage)
{
	if (!a_actor) {
		return;
	}

	auto* wanted = StageSpell(a_need, a_stage);
	for (std::size_t stage = 0; stage < kStageCount; ++stage) {
		auto* spell = g_forms.stageSpells[Index(a_need)][stage];
		if (!spell || spell == wanted) {
			continue;
		}
		if (a_actor->HasSpell(spell)) {
			a_actor->RemoveSpell(spell);
		}
	}

	if (wanted && !a_actor->HasSpell(wanted)) {
		a_actor->AddSpell(wanted);
	}
}

void SunHelm::ClearStageSpells(RE::Actor* a_actor, Need a_need)
{
	if (!a_actor) {
		return;
	}
	for (auto* spell : g_forms.stageSpells[Index(a_need)]) {
		if (spell && a_actor->HasSpell(spell)) {
			a_actor->RemoveSpell(spell);
		}
	}
}
