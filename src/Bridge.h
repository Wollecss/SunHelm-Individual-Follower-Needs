#pragma once

#include "Followers.h"

// Outbound feed for the CHIM bridge, and the only part of this plugin that knows anything outside
// it exists.
//
// This deliberately does not talk to CHIM. It keeps a small queue of "this follower changed"
// payloads and exposes them as Papyrus natives; the CHIM bridge mod's own script drains the queue
// and forwards each one with AIAgentFunctions.logMessageForActor. That keeps every CHIM assumption -
// the transport, the message type, the server's payload grammar - in the bridge, where it can be
// updated when CHIM changes, rather than compiled into a DLL that would then need rebuilding.
//
// It is also why this is a queue rather than a getter. A poll-and-read API would either send the
// same unchanged state on every tick or make the script do the comparing; queuing only what has
// actually changed means the bridge sends one message per real event.
namespace Bridge
{
	// Registers the Papyrus natives. Safe to call when nothing consumes them.
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);

	// Called once per tracked follower per tick. Computes a signature of what CHIM would care
	// about and enqueues a payload only when that signature differs from the last one queued for
	// this follower, so a follower standing still produces no traffic at all.
	void NoteFollower(const Followers::State& a_state, RE::Actor& a_actor);

	// Queues a removal so the server stops describing someone who left the party.
	void NoteDismissed(RE::FormID a_formID, std::string_view a_name);

	// Drops everything queued and forgets what was last reported, so the next tick re-publishes
	// from scratch. Used on save load, where the party and their state may be entirely different.
	void Reset();
}
