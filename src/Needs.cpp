#include "Needs.h"

#include "Feeding.h"
#include "Followers.h"
#include "Settings.h"
#include "SunHelm.h"

namespace
{
	// Below this stage a need isn't worth interrupting the player about - stages 0 and 1 are
	// SunHelm's two "you're fine" bands.
	constexpr int kAnnounceFromStage = 2;

	std::string ToLower(std::string_view a_text)
	{
		std::string out{ a_text };
		std::ranges::transform(out, out.begin(), [](unsigned char a_ch) {
			return static_cast<char>(std::tolower(a_ch));
		});
		return out;
	}

	bool IsNeedTracked(SunHelm::Need a_need)
	{
		const auto& settings = Settings::Get();
		switch (a_need) {
		case SunHelm::Need::kHunger:
			return settings.trackHunger;
		case SunHelm::Need::kThirst:
			return settings.trackThirst;
		case SunHelm::Need::kFatigue:
			return settings.mirrorFatigue;
		case SunHelm::Need::kCold:
			return settings.mirrorCold;
		default:
			return false;
		}
	}

	// Whether accumulation should be frozen right now, honouring SunHelm's own pause settings so
	// followers behave the same way the player does.
	bool IsPaused(RE::Actor& a_actor)
	{
		if (SunHelm::PausesInCombat() && a_actor.IsInCombat()) {
			return true;
		}
		if (SunHelm::PausesInDialogue()) {
			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				return true;
			}
		}
		return false;
	}

	void Announce(const Followers::State& a_state, SunHelm::Need a_need, int a_stage)
	{
		const auto text = std::format("{} is {}.", a_state.name, ToLower(SunHelm::StageLabel(a_need, a_stage)));
		RE::DebugNotification(text.c_str());
	}

	void AdvanceOwnNeed(Followers::State& a_state, SunHelm::Need a_need, float a_elapsedHours)
	{
		if (!IsNeedTracked(a_need) || !SunHelm::IsNeedEnabled(a_need)) {
			return;
		}

		// SunHelm's rate globals are units per game hour, read live so followers always move at
		// whatever pace the player has SunHelm set to.
		const auto gain = a_elapsedHours * SunHelm::Rate(a_need);
		auto&      value = a_need == SunHelm::Need::kHunger ? a_state.hunger : a_state.thirst;
		value = std::clamp(value + gain, 0.0f, SunHelm::MaxLevel(a_need));
	}

	void SyncStage(Followers::State& a_state, RE::Actor& a_actor, SunHelm::Need a_need, int a_stage)
	{
		const auto  index = static_cast<std::size_t>(a_need);
		const auto& settings = Settings::Get();

		const auto tracked = IsNeedTracked(a_need) && SunHelm::IsNeedEnabled(a_need);

		if (!tracked || !settings.applyDebuffs) {
			// Only strip abilities once, when the need stops being applied, rather than issuing
			// RemoveSpell calls on every poll.
			if (a_state.appliedStage[index] != -1) {
				SunHelm::ClearStageSpells(&a_actor, a_need);
				a_state.appliedStage[index] = -1;
			}
			return;
		}

		if (a_state.appliedStage[index] != a_stage) {
			SunHelm::ApplyStageSpell(&a_actor, a_need, a_stage);
			a_state.appliedStage[index] = a_stage;
		}
	}

	void MaybeAnnounce(Followers::State& a_state, SunHelm::Need a_need, int a_stage)
	{
		// Only hunger and thirst have somewhere to remember the last announced stage; fatigue and
		// cold are mirrored from the player, who already gets SunHelm's own message for them.
		if (a_need != SunHelm::Need::kHunger && a_need != SunHelm::Need::kThirst) {
			return;
		}

		const auto slot = a_need == SunHelm::Need::kHunger ? 0 : 1;
		const auto previous = a_state.lastNotifiedStage[slot];
		a_state.lastNotifiedStage[slot] = a_stage;

		if (!Settings::Get().notifyNeed[static_cast<std::size_t>(a_need)]) {
			return;
		}
		// Announce only when things get worse, and only once per crossing. Improvement is covered
		// by the eating and drinking messages.
		if (previous >= 0 && a_stage > previous && a_stage >= kAnnounceFromStage) {
			Announce(a_state, a_need, a_stage);
		}
	}

	void ApplyNeedsDamage(const Followers::State& a_state, RE::Actor& a_actor, float a_elapsedHours)
	{
		if (!Settings::Get().needsDamage || a_elapsedHours <= 0.0f) {
			return;
		}

		int maxed = 0;
		if (Settings::Get().trackHunger && SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger) == 5) {
			++maxed;
		}
		if (Settings::Get().trackThirst && SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst) == 5) {
			++maxed;
		}
		if (maxed == 0) {
			return;
		}

		// Deliberately gentler than the 1-75 per hour SunHelm rolls against the player: a follower
		// who goes down is a loss you can't heal your way out of, and this is a toggle people turn
		// on for pressure rather than for losing companions.
		const auto damage = a_elapsedHours * 5.0f * static_cast<float>(maxed);
		a_actor.AsActorValueOwner()->RestoreActorValue(
			RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -damage);
	}
}

void Needs::Update()
{
	if (!SunHelm::IsAvailable() || !Settings::Get().enabled || !SunHelm::IsModEnabled()) {
		return;
	}

	const auto nowHours = RE::Calendar::GetSingleton()->GetHoursPassed();

	// Logged once, on the first tick after a save is actually live, so the numbers can be compared
	// against SunHelm's own widget.
	static bool loggedLiveValues = false;
	if (!loggedLiveValues) {
		loggedLiveValues = true;
		logger::info("SunHelm live: hunger={:.1f}@{:.1f}/hr thirst={:.1f}@{:.1f}/hr fatigue={:.1f} cold={:.1f}",
			SunHelm::PlayerLevel(SunHelm::Need::kHunger), SunHelm::Rate(SunHelm::Need::kHunger),
			SunHelm::PlayerLevel(SunHelm::Need::kThirst), SunHelm::Rate(SunHelm::Need::kThirst),
			SunHelm::PlayerLevel(SunHelm::Need::kFatigue), SunHelm::PlayerLevel(SunHelm::Need::kCold));
	}

	Followers::ForEachTracked([&](Followers::State& a_state, RE::Actor& a_actor) {
		auto elapsed = nowHours - a_state.lastTickHours;
		a_state.lastTickHours = nowHours;

		// A negative delta means the clock moved backwards - a save from earlier in the timeline
		// was loaded. Skip the tick rather than trusting the number.
		if (elapsed < 0.0f) {
			elapsed = 0.0f;
		}

		// Frozen cases still take the timestamp above, so time spent parked or paused is skipped
		// instead of landing all at once when things resume.
		if (a_state.active && !IsPaused(a_actor)) {
			if (elapsed > 0.0f) {
				AdvanceOwnNeed(a_state, SunHelm::Need::kHunger, elapsed);
				AdvanceOwnNeed(a_state, SunHelm::Need::kThirst, elapsed);
				ApplyNeedsDamage(a_state, a_actor, elapsed);
			}
			// Runs every tick regardless of elapsed time, so a follower fed by the player mid-poll
			// still gets picked up promptly rather than waiting on the next hour of game time.
			Feeding::TryEatAndDrink(a_state, a_actor);
		}

		for (const auto need : SunHelm::kAllNeeds) {
			int stage = 0;
			switch (need) {
			case SunHelm::Need::kHunger:
				stage = SunHelm::StageOf(need, a_state.hunger);
				break;
			case SunHelm::Need::kThirst:
				stage = SunHelm::StageOf(need, a_state.thirst);
				break;
			default:
				// Fatigue and cold are read straight off the player, so taking care of your own
				// sleep and warmth takes care of your followers' at the same time.
				stage = SunHelm::StageOf(need, SunHelm::PlayerLevel(need));
				break;
			}

			MaybeAnnounce(a_state, need, stage);
			SyncStage(a_state, a_actor, need, stage);
		}
	});
}
