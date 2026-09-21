#include "UI.h"

#include "Followers.h"
#include "Settings.h"
#include "SunHelm.h"

void UI::Register()
{
	// The menu is optional: without the framework installed the plugin still runs on its defaults.
	// Every call in the framework header is null-guarded, so this is a soft dependency.
	if (!SKSEMenuFramework::IsInstalled()) {
		logger::info("SKSE Menu Framework not found - running without a config UI");
		return;
	}

	SKSEMenuFramework::SetSection("SunHelm Follower Needs");
	SKSEMenuFramework::AddSectionItem("Status", Status::Render);
	SKSEMenuFramework::AddSectionItem("Settings", Config::Render);

	logger::info("Config UI registered (framework version {:.2f})", SKSEMenuFramework::GetMenuFrameworkVersion());
}

void __stdcall UI::Status::Render()
{
	if (!SunHelm::IsAvailable()) {
		ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "SunHelm was not detected.");
		ImGuiMCP::TextWrapped("SunHelmSurvival.esp has to be loaded for follower needs to run.");
		return;
	}

	ImGuiMCP::SeparatorText("Tracked followers");

	const auto followers = Followers::Snapshot();
	if (followers.empty()) {
		ImGuiMCP::TextWrapped("Nobody is being tracked. Recruit a follower and they'll appear here.");
	} else if (ImGuiMCP::BeginTable("tracked_followers", 4,
				   ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersOuter |
					   ImGuiMCP::ImGuiTableFlags_BordersV)) {
		ImGuiMCP::TableSetupColumn("Follower");
		ImGuiMCP::TableSetupColumn("Hunger");
		ImGuiMCP::TableSetupColumn("Thirst");
		ImGuiMCP::TableSetupColumn("With you");
		ImGuiMCP::TableHeadersRow();

		for (const auto& follower : followers) {
			const auto hungerStage = SunHelm::StageOf(SunHelm::Need::kHunger, follower.hunger);
			const auto thirstStage = SunHelm::StageOf(SunHelm::Need::kThirst, follower.thirst);

			ImGuiMCP::TableNextRow();
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%s", follower.name.c_str());
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%.0f - %s", follower.hunger,
				std::string(SunHelm::StageLabel(SunHelm::Need::kHunger, hungerStage)).c_str());
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%.0f - %s", follower.thirst,
				std::string(SunHelm::StageLabel(SunHelm::Need::kThirst, thirstStage)).c_str());
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%s", follower.active ? "yes" : "waiting");
		}

		ImGuiMCP::EndTable();
	}

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("SunHelm (the player's own needs)");
	ImGuiMCP::Text("SunHelm is %s", SunHelm::IsModEnabled() ? "running" : "stopped");

	// Fatigue and cold are read straight off these values rather than simulated per follower, so
	// seeing them here is also how you check what followers are mirroring.
	if (ImGuiMCP::BeginTable("player_needs", 5,
			ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersOuter |
				ImGuiMCP::ImGuiTableFlags_BordersV)) {
		ImGuiMCP::TableSetupColumn("Need");
		ImGuiMCP::TableSetupColumn("Level");
		ImGuiMCP::TableSetupColumn("Stage");
		ImGuiMCP::TableSetupColumn("Rate");
		ImGuiMCP::TableSetupColumn("On");
		ImGuiMCP::TableHeadersRow();

		for (const auto need : SunHelm::kAllNeeds) {
			const auto level = SunHelm::PlayerLevel(need);
			const auto stage = SunHelm::StageOf(need, level);

			ImGuiMCP::TableNextRow();
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%s", std::string(SunHelm::NeedName(need)).c_str());
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%.1f", level);
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%d - %s", stage, std::string(SunHelm::StageLabel(need, stage)).c_str());
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%.2f", SunHelm::Rate(need));
			ImGuiMCP::TableNextColumn();
			ImGuiMCP::Text("%s", SunHelm::IsNeedEnabled(need) ? "yes" : "no");
		}

		ImGuiMCP::EndTable();
	}
}

namespace
{
	// A checkbox is a single discrete click, so its change is final the moment it returns true and
	// can be written straight out. A slider reports true on every frame of a drag, so that one
	// waits for the edit to actually finish rather than hammering the file.
	bool Toggle(const char* a_label, bool* a_value)
	{
		if (ImGuiMCP::Checkbox(a_label, a_value)) {
			Settings::Save();
			return true;
		}
		return false;
	}

	void SaveIfSliderReleased()
	{
		if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
			Settings::Save();
		}
	}
}

void __stdcall UI::Config::Render()
{
	auto& settings = Settings::Get();

	Toggle("Track follower needs", &settings.enabled);
	ImGuiMCP::SetItemTooltip("Turns the whole mod off without uninstalling it.");

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("How many followers");

	static int  pendingLimit = settings.maxTracked;
	static bool limitWarningShown = false;
	constexpr auto kLimitPopup = "Raise the tracking limit?";

	int limit = settings.maxTracked;
	if (ImGuiMCP::SliderInt("Followers tracked", &limit, 1, Settings::kMaxSlots)) {
		if (limit > Settings::kDefaultTracked && !limitWarningShown) {
			pendingLimit = limit;
			ImGuiMCP::OpenPopup(kLimitPopup);
		} else {
			settings.maxTracked = limit;
		}
	}
	SaveIfSliderReleased();
	ImGuiMCP::SetItemTooltip(
		"How many followers can have hunger and thirst tracked at once. Tracking runs natively "
		"rather than in Papyrus, so the performance cost is minimal - raising this mainly means a "
		"slightly larger save file per follower being tracked.");

	if (ImGuiMCP::BeginPopupModal(kLimitPopup)) {
		ImGuiMCP::TextWrapped(
			"The default of %d keeps things light for most setups. Each extra follower being "
			"tracked adds a little more state to your save file. It will not cause the script lag "
			"that heavier follower mods can, because none of this runs in Papyrus - but it is not "
			"free either.",
			Settings::kDefaultTracked);
		ImGuiMCP::Spacing();
		if (ImGuiMCP::Button("Track more")) {
			settings.maxTracked = pendingLimit;
			limitWarningShown = true;
			Settings::Save();
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Leave it alone")) {
			ImGuiMCP::CloseCurrentPopup();
		}
		ImGuiMCP::EndPopup();
	}

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("Which needs");

	Toggle("Hunger", &settings.trackHunger);
	ImGuiMCP::SetItemTooltip("Tracked per follower, and relieved when they eat.");
	Toggle("Thirst", &settings.trackThirst);
	ImGuiMCP::SetItemTooltip("Tracked per follower, and relieved when they drink.");
	Toggle("Fatigue", &settings.mirrorFatigue);
	ImGuiMCP::SetItemTooltip(
		"Mirrors your own fatigue rather than being tracked separately, so sleeping rests your "
		"followers at the same time it rests you.");
	Toggle("Cold", &settings.mirrorCold);
	ImGuiMCP::SetItemTooltip(
		"Mirrors your own cold rather than being tracked separately, so getting yourself warm "
		"warms your followers too.");

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("Effects");

	Toggle("Apply penalties to followers", &settings.applyDebuffs);
	ImGuiMCP::SetItemTooltip(
		"Gives followers the same stage penalties SunHelm gives you. Off means needs are still "
		"tracked and reported, but carry no mechanical effect.");
	Toggle("Needs can damage health", &settings.needsDamage);
	ImGuiMCP::SetItemTooltip(
		"Off by default. Losing a companion because you forgot to feed them is a harsher failure "
		"than taking the same damage yourself.");

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("Eating and drinking");

	Toggle("Followers feed themselves", &settings.allowSelfFeeding);
	ImGuiMCP::SetItemTooltip(
		"Followers eat and drink out of their own inventory when they need to, consuming the item. "
		"Off means they only improve when something else feeds them.");

	const auto stageCombo = [](const char* a_label, int* a_stage, SunHelm::Need a_need) {
		if (ImGuiMCP::SliderInt(a_label, a_stage, 1, 5,
				std::string(SunHelm::StageLabel(a_need, *a_stage)).c_str())) {
			*a_stage = std::clamp(*a_stage, 1, 5);
		}
		SaveIfSliderReleased();
	};
	stageCombo("Eat once they are", &settings.eatAtStage, SunHelm::Need::kHunger);
	ImGuiMCP::SetItemTooltip("The hunger stage at which a follower will reach for food.");
	stageCombo("Drink once they are", &settings.drinkAtStage, SunHelm::Need::kThirst);
	ImGuiMCP::SetItemTooltip("The thirst stage at which a follower will reach for a drink.");

	ImGuiMCP::Spacing();
	ImGuiMCP::SeparatorText("Notifications");

	for (const auto need : SunHelm::kAllNeeds) {
		const auto index = static_cast<std::size_t>(need);
		const auto label = std::format("Announce {}", SunHelm::NeedName(need));
		Toggle(label.c_str(), &settings.notifyNeed[index]);
	}
	Toggle("Announce eating and drinking", &settings.notifyConsumption);
}
