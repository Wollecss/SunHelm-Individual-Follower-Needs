#pragma once

namespace Needs
{
	// Advances hunger and thirst, mirrors fatigue and cold off the player, keeps each follower's
	// stage ability in step and announces stage changes. Main thread only.
	void Update();
}
