#pragma once

namespace Persistence
{
	// Bumped only if the meaning of the slot globals changes. Mismatched save data is discarded
	// rather than misread.
	inline constexpr int kSchemaVersion = 1;

	// Resolves the slot globals out of SunHelmFollowerNeeds.esp. Returns false if the plugin isn't
	// loaded, in which case everything still runs, just without surviving a save/load.
	bool Resolve();
	bool IsAvailable();

	// Mirrors the tracked set into the globals. Called every tick: the engine captures whatever a
	// GlobalVariable holds at the moment the player saves, so keeping them continuously current
	// means there's no save event to hook and no way to miss a save.
	void Save();

	// Reads the globals into the pending-restore set. Called once a save is live, when the globals
	// hold that save's values rather than the ESP's defaults.
	void Load();
}
