#pragma once

#include "Followers.h"

namespace Feeding
{
	// Looks for food/drink in the follower's own inventory when a tracked need is due, and if
	// found, triggers the real consume action on them (same native path the engine uses for any
	// actor "using" a potion) rather than faking an animation. Main thread only.
	void TryEatAndDrink(Followers::State& a_state, RE::Actor& a_actor);
}
