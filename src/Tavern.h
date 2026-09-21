#pragma once

#include "Followers.h"

namespace Tavern
{
	// If the follower is inside an inn, needs something, and can afford it, buys it out of their own
	// purse and puts it in their pack. Deliberately does NOT apply any relief: the normal feeding
	// path picks the item up on a later tick and handles consuming it, so there is exactly one code
	// path for eating and drinking. Main thread only.
	//
	// Returns true if something was bought.
	bool TryPurchase(Followers::State& a_state, RE::Actor& a_actor);
}
