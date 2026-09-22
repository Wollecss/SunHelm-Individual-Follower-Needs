#include "EAS.h"

namespace
{
	// How EAS actually works, because it is not what it looks like from the outside.
	//
	// Its quest alias script watches the *player* equip something and casts a per-item spell
	// (aaz_<Item>_Animation_SP) on them. That spell's magic effect carries a keyword, and EAS ships
	// DAR conditions of the form HasMagicEffectWithKeyword(...) that swap in the eating animation
	// for whoever holds that effect. Neither the magic effect script nor the DAR condition asks who
	// the actor is - the effect script works off akCaster, and DAR only tests the effect. Only the
	// trigger is player-only.
	//
	// So a follower animates if the right spell is cast on them, with no behaviour-graph work at
	// all. An earlier version of this comment (and the readme) claimed the opposite. That was
	// wrong, and a user proved it by building this and telling us it worked.
	//
	// The item side comes from EAS's KID ini, which tags every consumable it covers with a
	// per-item keyword named EASkey_<Item> - the same <Item> as the spell. That is the join:
	// keyword on the item -> name -> animation spell. Going through the keyword rather than a list
	// of item FormIDs means anything a user adds to the KID ini is picked up for free.
	constexpr auto kKeywordPrefix = "EASkey_"sv;
	constexpr auto kSpellPrefix = "aaz_"sv;
	constexpr auto kSpellSuffix = "_Animation_SP"sv;

	struct StockSpell
	{
		std::string_view name;
		RE::FormID       localID;
	};

	// Read straight out of TaberuAnimation.esp. Only used when the EditorID scan below comes up
	// empty, which happens when powerofthree's Tweaks isn't installed to cache EditorIDs for form
	// types that don't keep one at runtime - spells among them. Without this fallback the whole
	// feature would silently depend on an unrelated mod being present.
	constexpr std::array kStockSpells{
		StockSpell{ "Ale"sv, 0x2AD41E },
		StockSpell{ "AltoWine01"sv, 0x2AD41F },
		StockSpell{ "AltoWine02"sv, 0x2AD455 },
		StockSpell{ "AppleCabbageStew"sv, 0x2AD423 },
		StockSpell{ "ArgonianBloodWine"sv, 0x2AD425 },
		StockSpell{ "AshHopperLeg"sv, 0x2AD427 },
		StockSpell{ "AshHopperMeat"sv, 0x2AD429 },
		StockSpell{ "AshYam"sv, 0x2AD42B },
		StockSpell{ "BakedPotatoes"sv, 0x2AD42D },
		StockSpell{ "BeefStew"sv, 0x2AD42F },
		StockSpell{ "BlackBriarMeadPrivateReserve"sv, 0x2AD433 },
		StockSpell{ "BlackBriarMead"sv, 0x2AD431 },
		StockSpell{ "BoarMeat"sv, 0x2AD435 },
		StockSpell{ "BoiledCremeTreat"sv, 0x2AD437 },
		StockSpell{ "BraidedBread"sv, 0x2AD439 },
		StockSpell{ "BreadHalf"sv, 0x2AD43D },
		StockSpell{ "Bread"sv, 0x2AD43B },
		StockSpell{ "Butter"sv, 0x2AD43F },
		StockSpell{ "CabbagePotatoSoup"sv, 0x2AD443 },
		StockSpell{ "CabbageSoup"sv, 0x2AD445 },
		StockSpell{ "Cabbage"sv, 0x2AD441 },
		StockSpell{ "Carrot"sv, 0x2AD447 },
		StockSpell{ "CharredSkeeverMeat"sv, 0x2AD449 },
		StockSpell{ "ChickenBreast"sv, 0x2AD44B },
		StockSpell{ "ClamChowder"sv, 0x2AD44D },
		StockSpell{ "ClamMeat"sv, 0x2AD44F },
		StockSpell{ "CookedBeef"sv, 0x2AD451 },
		StockSpell{ "CookedBoarMeat"sv, 0x2AD453 },
		StockSpell{ "DogMeat"sv, 0x2AD457 },
		StockSpell{ "Dumpling"sv, 0x2AD458 },
		StockSpell{ "EidarCheeseWedge"sv, 0x2AD45B },
		StockSpell{ "EidarCheeseWheel"sv, 0x2AD45D },
		StockSpell{ "ElsweyrFondue"sv, 0x2AD45F },
		StockSpell{ "FirebrandWine"sv, 0x2AD461 },
		StockSpell{ "Flin"sv, 0x2AD463 },
		StockSpell{ "GarlicBread"sv, 0x2AD465 },
		StockSpell{ "GoatCheeseWedge"sv, 0x2AD467 },
		StockSpell{ "GoatCheeseWheel"sv, 0x2AD469 },
		StockSpell{ "Gourd"sv, 0x2AD46B },
		StockSpell{ "GreenApple"sv, 0x2AD46D },
		StockSpell{ "GrilledChickenBreast"sv, 0x2AD46F },
		StockSpell{ "GrilledLeeks"sv, 0x2AD471 },
		StockSpell{ "HoneyNutTreat"sv, 0x2AD475 },
		StockSpell{ "Honey"sv, 0x2AD473 },
		StockSpell{ "HonningbrewMead"sv, 0x2AD477 },
		StockSpell{ "HorkerAndAshYamStew"sv, 0x2AD479 },
		StockSpell{ "HorkerLoaf"sv, 0x2AD47B },
		StockSpell{ "HorkerMeat"sv, 0x2AD47D },
		StockSpell{ "HorkerStew"sv, 0x2AD47F },
		StockSpell{ "HorseHaunch"sv, 0x2AD481 },
		StockSpell{ "HorseMeat"sv, 0x2AD483 },
		StockSpell{ "JazbayCrostata"sv, 0x2AD485 },
		StockSpell{ "JugOfMilk"sv, 0x2AD487 },
		StockSpell{ "JuniperBerryCrostata"sv, 0x2AD489 },
		StockSpell{ "Leek"sv, 0x2AD48B },
		StockSpell{ "LegOfGoat"sv, 0x2AD48D },
		StockSpell{ "LegofGoatRoast"sv, 0x2AD48F },
		StockSpell{ "LongTaffyTreat"sv, 0x2AD491 },
		StockSpell{ "MammothCheeseBowl"sv, 0x2AD493 },
		StockSpell{ "MammothSnout"sv, 0x2AD495 },
		StockSpell{ "MammothSteak"sv, 0x2AD497 },
		StockSpell{ "Matze"sv, 0x2AD499 },
		StockSpell{ "MudcrabLegs"sv, 0x2AD49B },
		StockSpell{ "PheasantBreast"sv, 0x2AD49D },
		StockSpell{ "PheasantRoast"sv, 0x2AD49F },
		StockSpell{ "Pie"sv, 0x2AD4A1 },
		StockSpell{ "PotatoBread"sv, 0x2AD4A5 },
		StockSpell{ "PotatoSoup"sv, 0x2AD4A7 },
		StockSpell{ "Potato"sv, 0x2AD4A3 },
		StockSpell{ "RabbitHaunch"sv, 0x2AD4A9 },
		StockSpell{ "RawBeef"sv, 0x2AD4AB },
		StockSpell{ "RawRabbitLeg"sv, 0x2AD4AD },
		StockSpell{ "RedApple"sv, 0x2AD4AF },
		StockSpell{ "SackOfFlour"sv, 0x2AD4B1 },
		StockSpell{ "SalmonMeat"sv, 0x2AD4B3 },
		StockSpell{ "SalmonSteakHF"sv, 0x2AD4B7 },
		StockSpell{ "SalmonSteak"sv, 0x2AD4B5 },
		StockSpell{ "SearedSlaughterfish"sv, 0x2AD4B9 },
		StockSpell{ "Shein"sv, 0x2AD4BB },
		StockSpell{ "SlicedEidarCheese"sv, 0x2AD4BD },
		StockSpell{ "SlicedGoatCheese"sv, 0x2AD4BF },
		StockSpell{ "SnowberryCrostata"sv, 0x2AD4C1 },
		StockSpell{ "SoulHusk"sv, 0x2AD4C3 },
		StockSpell{ "SpicedWine"sv, 0x2AD4C5 },
		StockSpell{ "SteamedMudcrabLegs"sv, 0x2AD4C7 },
		StockSpell{ "Suitou"sv, 0x330FFF },
		StockSpell{ "Sujamma"sv, 0x2AD4C9 },
		StockSpell{ "SurilieBrothersWine"sv, 0x2AD4CB },
		StockSpell{ "SweetRoll"sv, 0x2AD4CD },
		StockSpell{ "TomatoSoup"sv, 0x2AD4D1 },
		StockSpell{ "Tomato"sv, 0x2AD4CF },
		StockSpell{ "VegetableSoup"sv, 0x2AD4D3 },
		StockSpell{ "VelvetLeChance"sv, 0x317AFA },
		StockSpell{ "VenisonChop"sv, 0x2AD4D7 },
		StockSpell{ "VenisonStew"sv, 0x2AD4D9 },
		StockSpell{ "Venison"sv, 0x2AD4D5 },
		StockSpell{ "Wine01"sv, 0x2AD4DB },
		StockSpell{ "Wine02"sv, 0x2AD4DD },
	};

	std::unordered_map<std::string, RE::SpellItem*> g_spells;

	// -1 means "not counted yet". See CoveredItemCount() for why this isn't done at load.
	int g_coveredItems{ -1 };

	RE::SpellItem* SpellFor(RE::TESBoundObject* a_object)
	{
		if (g_spells.empty() || !a_object) {
			return nullptr;
		}
		const auto* keyworded = a_object->As<RE::BGSKeywordForm>();
		if (!keyworded) {
			return nullptr;
		}

		for (const auto* keyword : keyworded->GetKeywords()) {
			if (!keyword) {
				continue;
			}
			const auto* editorID = keyword->GetFormEditorID();
			if (!editorID) {
				continue;
			}
			std::string_view id{ editorID };
			if (!id.starts_with(kKeywordPrefix)) {
				continue;
			}
			id.remove_prefix(kKeywordPrefix.size());
			if (const auto it = g_spells.find(std::string{ id }); it != g_spells.end()) {
				return it->second;
			}
		}
		return nullptr;
	}

	// Matches aaz_<name>_Animation_SP and hands back <name>. Empty when it isn't one of EAS's.
	std::string_view AnimationNameOf(const char* a_editorID)
	{
		if (!a_editorID) {
			return {};
		}
		std::string_view id{ a_editorID };
		if (!id.starts_with(kSpellPrefix) || !id.ends_with(kSpellSuffix) ||
			id.size() <= kSpellPrefix.size() + kSpellSuffix.size()) {
			return {};
		}
		id.remove_prefix(kSpellPrefix.size());
		id.remove_suffix(kSpellSuffix.size());
		return id;
	}
}

bool EAS::Resolve()
{
	g_spells.clear();
	g_coveredItems = -1;

	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		return false;
	}

	// Preferred route: match on EditorID, which also picks up EAS addon plugins that follow the
	// same naming, and survives EAS renumbering its forms.
	for (auto* spell : handler->GetFormArray<RE::SpellItem>()) {
		if (!spell) {
			continue;
		}
		if (const auto name = AnimationNameOf(spell->GetFormEditorID()); !name.empty()) {
			g_spells.emplace(name, spell);
		}
	}
	const auto byEditorID = g_spells.size();

	if (g_spells.empty()) {
		for (const auto& [name, localID] : kStockSpells) {
			if (auto* spell = handler->LookupForm<RE::SpellItem>(localID, kPluginName)) {
				g_spells.emplace(name, spell);
			}
		}
	}

	if (g_spells.empty()) {
		// Absent is the ordinary case and says so quietly; present-but-unresolvable is a real
		// problem, and saying nothing would leave it looking exactly like not having the mod.
		if (handler->LookupModByName(kPluginName)) {
			logger::error("{} is installed but none of its animation spells resolved - followers "
						  "will eat and drink without animation",
				kPluginName);
		} else {
			logger::info("Eating Animations and Sounds not installed; followers eat and drink "
						 "without animation");
		}
		return false;
	}

	logger::info("Eating Animations and Sounds resolved ({} animations, via {})", g_spells.size(),
		byEditorID > 0 ? "EditorID" : "the plugin's own FormIDs");
	return true;
}

bool EAS::IsAvailable()
{
	return !g_spells.empty();
}

int EAS::AnimationCount()
{
	return static_cast<int>(g_spells.size());
}

int EAS::CoveredItemCount()
{
	if (g_coveredItems >= 0) {
		return g_coveredItems;
	}
	if (g_spells.empty()) {
		return 0;
	}

	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		return 0;  // Deliberately not cached - this is a failure to count, not a count of zero.
	}

	int count = 0;
	for (auto* item : handler->GetFormArray<RE::AlchemyItem>()) {
		if (SpellFor(item)) {
			++count;
		}
	}
	g_coveredItems = count;
	return count;
}

bool EAS::Play(RE::Actor& a_actor, RE::TESBoundObject* a_object)
{
	auto* spell = SpellFor(a_object);
	if (!spell) {
		return false;
	}
	auto* caster = a_actor.GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
	if (!caster) {
		return false;
	}

	// The follower is both target and blame actor, which is what EAS's own RemoteCast on the player
	// amounts to. Its effect script reads akCaster, so passing anyone else here would animate the
	// wrong actor - or nobody.
	caster->CastSpellImmediate(spell, false, &a_actor, 1.0f, false, 0.0f, &a_actor);
	return true;
}
