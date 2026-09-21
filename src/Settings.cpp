#include "Settings.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
	// Relative to the game's working directory, the same way the menu framework's own
	// IsInstalled() check resolves its DLL path. Under MO2 this lands in the virtual Data folder.
	constexpr auto kSettingsPath = "Data/SKSE/Plugins/SunHelmFollowerNeeds.json";

	Settings::Data g_data;

	// Every field is read with a default of whatever the struct already holds, so a file written by
	// an older build (or a hand-edited one missing keys) keeps working - absent keys just keep
	// their default rather than resetting the whole file.
	template <class T>
	void ReadInto(const json& a_json, const char* a_key, T& a_target)
	{
		if (a_json.contains(a_key)) {
			try {
				a_target = a_json.at(a_key).get<T>();
			} catch (const std::exception& ex) {
				logger::warn("Setting '{}' could not be read ({}), keeping default", a_key, ex.what());
			}
		}
	}

	constexpr std::array kNeedKeys{
		"notifyHunger", "notifyThirst", "notifyFatigue", "notifyCold"
	};
}

Settings::Data& Settings::Get()
{
	return g_data;
}

void Settings::Load()
{
	std::ifstream file(kSettingsPath);
	if (!file) {
		logger::info("No settings file yet - using defaults");
		return;
	}

	json parsed;
	try {
		file >> parsed;
	} catch (const std::exception& ex) {
		logger::error("Settings file is malformed ({}) - using defaults and leaving it alone", ex.what());
		return;
	}

	ReadInto(parsed, "enabled", g_data.enabled);
	ReadInto(parsed, "maxTracked", g_data.maxTracked);
	ReadInto(parsed, "trackHunger", g_data.trackHunger);
	ReadInto(parsed, "trackThirst", g_data.trackThirst);
	ReadInto(parsed, "mirrorFatigue", g_data.mirrorFatigue);
	ReadInto(parsed, "mirrorCold", g_data.mirrorCold);
	ReadInto(parsed, "applyDebuffs", g_data.applyDebuffs);
	ReadInto(parsed, "allowSelfFeeding", g_data.allowSelfFeeding);
	ReadInto(parsed, "eatAtStage", g_data.eatAtStage);
	ReadInto(parsed, "drinkAtStage", g_data.drinkAtStage);
	ReadInto(parsed, "notifyConsumption", g_data.notifyConsumption);
	ReadInto(parsed, "needsDamage", g_data.needsDamage);
	ReadInto(parsed, "pollSeconds", g_data.pollSeconds);

	for (std::size_t need = 0; need < kNeedKeys.size(); ++need) {
		ReadInto(parsed, kNeedKeys[need], g_data.notifyNeed[need]);
	}

	// Clamped after reading rather than trusted: the file is editable by hand, and an out-of-range
	// slot count would leave Persistence looking for globals the ESP doesn't declare.
	g_data.maxTracked = std::clamp(g_data.maxTracked, 1, kMaxSlots);
	g_data.eatAtStage = std::clamp(g_data.eatAtStage, 1, 5);
	g_data.drinkAtStage = std::clamp(g_data.drinkAtStage, 1, 5);
	g_data.pollSeconds = std::clamp(g_data.pollSeconds, 1.0f, 60.0f);

	logger::info("Settings loaded (tracking up to {} follower(s))", g_data.maxTracked);
}

void Settings::Save()
{
	json out;
	out["enabled"] = g_data.enabled;
	out["maxTracked"] = g_data.maxTracked;
	out["trackHunger"] = g_data.trackHunger;
	out["trackThirst"] = g_data.trackThirst;
	out["mirrorFatigue"] = g_data.mirrorFatigue;
	out["mirrorCold"] = g_data.mirrorCold;
	out["applyDebuffs"] = g_data.applyDebuffs;
	out["allowSelfFeeding"] = g_data.allowSelfFeeding;
	out["eatAtStage"] = g_data.eatAtStage;
	out["drinkAtStage"] = g_data.drinkAtStage;
	out["notifyConsumption"] = g_data.notifyConsumption;
	out["needsDamage"] = g_data.needsDamage;
	out["pollSeconds"] = g_data.pollSeconds;

	for (std::size_t need = 0; need < kNeedKeys.size(); ++need) {
		out[kNeedKeys[need]] = g_data.notifyNeed[need];
	}

	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path(), ec);

	std::ofstream file(kSettingsPath);
	if (!file) {
		logger::error("Could not write {}", kSettingsPath);
		return;
	}
	file << out.dump(2) << '\n';
}
