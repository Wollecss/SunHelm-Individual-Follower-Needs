#pragma once

// Skyrim 1.7 branch only. Restores CommonLibSSE-NG names this code uses that the maintained
// library renamed, so call sites stay identical to main's and merging main into this branch
// doesn't conflict on every notification. Anything that couldn't be shimmed this way is changed
// at the call site instead, and listed here so it's findable when merging.
//
//   ActorValueOwner::RestoreActorValue(modifier, av, amount)  ->  ModActorValue(modifier, av, amount)
//       Same vtable slot (06). Beware: the OLD library also had a ModActorValue, in slot 05, which
//       was a different function - so this is a rename onto a name that used to mean something
//       else. The three-argument form is what picks the right one. Feeding.cpp, Needs.cpp.
//
//   BGSListForm::ForEachForm callback takes TESForm* instead of TESForm&. SunHelm.cpp.

namespace RE
{
	// Was RE::DebugNotification; the maintained library calls it ShowHUDMessage. Not just the same
	// signature: both wrap relocation 52050 (SE) / 52933 (AE), so this is the identical engine
	// call. If a future library version brings DebugNotification back, this becomes a
	// redefinition error - a loud failure, which is the point.
	inline void DebugNotification(const char* a_notification, const char* a_soundToPlay = nullptr,
		bool a_cancelIfAlreadyQueued = true)
	{
		SendHUDMessage::ShowHUDMessage(a_notification, a_soundToPlay, a_cancelIfAlreadyQueued);
	}
}
