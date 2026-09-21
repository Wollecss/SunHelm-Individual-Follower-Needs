#include "Persistence.h"

#include "Followers.h"
#include "Settings.h"

namespace
{
	constexpr auto kPluginName = "SunHelmFollowerNeeds.esp"sv;

	struct Slot
	{
		RE::TESGlobal* refLo{ nullptr };
		RE::TESGlobal* refHi{ nullptr };
		RE::TESGlobal* hunger{ nullptr };
		RE::TESGlobal* thirst{ nullptr };

		bool Complete() const { return refLo && refHi && hunger && thirst; }
	};

	RE::TESGlobal*                              g_schemaVersion{ nullptr };
	std::array<Slot, Settings::kMaxSlots>        g_slots{};
	bool                                        g_available{ false };

	// Skyrim only writes a form into a save's ChangeForms if the engine considers it modified, and
	// that's driven by TESForm::AddChange - simply assigning TESGlobal::value may not be enough to
	// mark it dirty. 0x01 is the documented GLOB "value changed" flag. CommonLibSSE-NG doesn't
	// declare a ChangeFlags struct for TESGlobal, so this is best-effort: whether global writes
	// actually survive a save/load is verified in-game, not assumed here.
	void MarkChanged(RE::TESGlobal* a_global)
	{
		constexpr std::uint32_t kGlobalValueChanged = 0x01;
		a_global->AddChange(kGlobalValueChanged);
	}

	void SetGlobal(RE::TESGlobal* a_global, float a_value)
	{
		if (a_global->value == a_value) {
			return;  // Avoid flagging a form as dirty when nothing actually moved.
		}
		a_global->value = a_value;
		MarkChanged(a_global);
	}

	// A FormID needs 32 bits but a float32 only holds integers up to 2^24 exactly, so it's stored
	// as two 16-bit halves - each of which is exactly representable.
	RE::FormID ReadFormID(const Slot& a_slot)
	{
		const auto lo = static_cast<std::uint32_t>(std::lround(a_slot.refLo->value)) & 0xFFFF;
		const auto hi = static_cast<std::uint32_t>(std::lround(a_slot.refHi->value)) & 0xFFFF;
		return static_cast<RE::FormID>((hi << 16) | lo);
	}

	void WriteFormID(const Slot& a_slot, RE::FormID a_formID)
	{
		SetGlobal(a_slot.refLo, static_cast<float>(a_formID & 0xFFFF));
		SetGlobal(a_slot.refHi, static_cast<float>((a_formID >> 16) & 0xFFFF));
	}

	void ClearSlot(const Slot& a_slot)
	{
		SetGlobal(a_slot.refLo, 0.0f);
		SetGlobal(a_slot.refHi, 0.0f);
		SetGlobal(a_slot.hunger, 0.0f);
		SetGlobal(a_slot.thirst, 0.0f);
	}
}

bool Persistence::Resolve()
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	// Both lists have to be checked. ESL-flagged plugins live in a separate "light" load order, so
	// LookupLoadedModByName alone silently stops finding this plugin the moment it is flagged -
	// which is exactly what happened when it was, disabling persistence without any other symptom.
	const auto loaded = handler && (handler->LookupLoadedModByName(kPluginName) ||
									   handler->LookupLoadedLightModByName(kPluginName));
	if (!loaded) {
		logger::warn("{} is not loaded - follower needs will not survive a save/load", kPluginName);
		g_available = false;
		return false;
	}

	// Matched by EditorID rather than FormID for the same reason SunHelm's own globals are: a
	// TESGlobal keeps its EditorID at runtime, so this survives the plugin being renumbered, and
	// works whether or not it ends up ESL-flagged.
	std::unordered_map<std::string_view, RE::TESGlobal**> wanted{
		{ "_SHFN_SchemaVersion"sv, &g_schemaVersion },
	};

	// Held separately because the keys have to outlive the map lookups below.
	std::vector<std::string> names;
	names.reserve(static_cast<std::size_t>(Settings::kMaxSlots) * 4);
	for (int slot = 0; slot < Settings::kMaxSlots; ++slot) {
		const auto add = [&](const char* a_suffix, RE::TESGlobal** a_target) {
			names.push_back(std::format("_SHFN_Slot{}_{}", slot, a_suffix));
			wanted.emplace(names.back(), a_target);
		};
		add("RefLo", &g_slots[slot].refLo);
		add("RefHi", &g_slots[slot].refHi);
		add("Hunger", &g_slots[slot].hunger);
		add("Thirst", &g_slots[slot].thirst);
	}

	for (auto* global : handler->GetFormArray<RE::TESGlobal>()) {
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

	g_available = g_schemaVersion != nullptr;
	for (const auto& slot : g_slots) {
		if (!slot.Complete()) {
			g_available = false;
		}
	}

	if (!g_available) {
		logger::error("{} is loaded but its storage globals could not all be resolved", kPluginName);
		return false;
	}

	logger::info("Persistence ready ({} slots)", Settings::kMaxSlots);
	return true;
}

bool Persistence::IsAvailable()
{
	return g_available;
}

void Persistence::Save()
{
	if (!g_available) {
		return;
	}

	SetGlobal(g_schemaVersion, static_cast<float>(kSchemaVersion));

	// Slots are filled in snapshot order, which is by name - so which slot a follower lands in can
	// shift as the party changes. That's fine: restoration matches on the stored FormID, not on the
	// slot index.
	std::size_t slot = 0;
	for (const auto& follower : Followers::Snapshot()) {
		if (slot >= g_slots.size()) {
			break;
		}
		WriteFormID(g_slots[slot], follower.formID);
		SetGlobal(g_slots[slot].hunger, follower.hunger);
		SetGlobal(g_slots[slot].thirst, follower.thirst);
		++slot;
	}

	// Zero the rest so a dismissed follower doesn't linger and get restored later.
	for (; slot < g_slots.size(); ++slot) {
		ClearSlot(g_slots[slot]);
	}
}

void Persistence::Load()
{
	if (!g_available) {
		return;
	}

	const auto storedSchema = static_cast<int>(std::lround(g_schemaVersion->value));
	if (storedSchema != kSchemaVersion) {
		logger::warn("Stored schema version {} does not match {} - discarding saved needs",
			storedSchema, kSchemaVersion);
		return;
	}

	int restored = 0;
	for (const auto& slot : g_slots) {
		const auto formID = ReadFormID(slot);
		if (formID == 0) {
			continue;
		}
		// Seeded rather than applied directly: the actor may not be loaded yet this early, and the
		// poll loop would treat an un-loaded entry as dismissed and purge it. The value is applied
		// when that follower is next picked up.
		Followers::SeedRestoredNeeds(formID, slot.hunger->value, slot.thirst->value);
		logger::info("Read slot: {:08X} hunger={:.1f} thirst={:.1f}", formID, slot.hunger->value,
			slot.thirst->value);
		++restored;
	}

	// Logged even when nothing came back: "no line at all" is indistinguishable from "never ran",
	// which cost a whole test cycle to untangle once already.
	logger::info("Persistence::Load read {} populated slot(s) (schema {})", restored, storedSchema);
}
