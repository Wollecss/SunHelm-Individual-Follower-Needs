#pragma once

// Integration layer over Eating Animations and Sounds (TaberuAnimation.esp).
//
// EAS is a soft dependency in the strictest sense: nothing here is required for the mod to work,
// and every entry point below is safe to call when EAS isn't installed - they report unavailable
// and do nothing. There is no setting to tell the plugin whether EAS is present; it is detected.
namespace EAS
{
	inline constexpr auto kPluginName = "TaberuAnimation.esp"sv;

	// Resolves EAS's per-item animation spells. Call once data is loaded. Returns false when EAS
	// isn't installed, which is not an error.
	bool Resolve();
	bool IsAvailable();

	// How many animation spells resolved. Stable regardless of what else is installed, so it's the
	// honest number to show in the MCM.
	int AnimationCount();

	// How many consumables in the current load order actually carry an EAS keyword this plugin can
	// act on. Counted on first call rather than at load, because the keywords come from KID and
	// there is no ordering guarantee between its distribution pass and ours - asking too early
	// reports zero for a setup that works perfectly.
	int CoveredItemCount();

	// Plays the eating/drinking animation for this item on this actor. Returns false when EAS is
	// absent or doesn't cover the item, which is the normal case for most of the game's food.
	// Main thread only.
	bool Play(RE::Actor& a_actor, RE::TESBoundObject* a_object);
}
