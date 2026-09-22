# Changelog

## 1.4.0

**Followers eat and drink with animations.**

- With [Eating Animations and Sounds][eas] installed, a follower eating or drinking now plays the
  same animation the player gets. Detected rather than configured - no patch, no load order
  requirement, nothing to set up, and nothing changes if you don't have it.
- Coverage comes from EAS's own keywords rather than a fixed item list, so **every EAS patch mod you
  already run extends it to followers too** - Gourmet, Simple Hunting Overhaul, the SunHelm
  waterskin patch, Arcs Tankard, the Fishing patch and so on, with no work on either side.
- New setting **Animate eating and drinking** (on by default). Its tooltip reports what was actually
  detected in your load order, so "I turned it on and nothing happened" is answerable from the page.
- The log now says whether each meal animated, or whether the item simply isn't one EAS covers.

Previous versions of this mod's documentation stated that animating followers was impossible without
behaviour-graph work. That was wrong.
[hanshotfirst01](https://www.nexusmods.com/profile/hanshotfirst01) worked out how to do it and
proved it by forking the repo and building it - this feature exists because of them.

**Fixed**

- The log no longer reports `Failed to dispatch message to devbench` as an error on every startup.
  DevBench is a development tool almost nobody has installed, and its absence was the loudest line
  in an otherwise healthy log.

[eas]: https://www.nexusmods.com/skyrimspecialedition/mods/42602

---

## 1.3.0

**A night at the inn worth having.**

- Ale gets its own purchase cooldown, separate from meals, so an evening at a tavern costs what you
  choose rather than emptying a purse. Adjustable, for people without CHIM who want to control the
  pace themselves.
- A thirsty follower at a bar usually orders **ale rather than water**, because nobody walks into an
  inn and asks for water - and ale quenches half as much, so it's a real trade.
- Drunkenness is split into two separate timers: how long drinks **stack** toward getting drunk, and
  how long it takes to **sober up** after leaving. They want opposite things, and sharing one number
  made drunkenness either unreachable or permanent.
- Being drunk no longer stops a follower ordering. They carry on like anyone else at the bar, and
  the evening ends when they leave rather than when they hit a number.
- Follower condition is published for the CHIM - Needs and Diseases Bridge to forward to
  HerikaServer, so companions can talk about their own hunger, thirst and drink. Entirely optional
  and inert without it.

**Fixed**

- **Crash on entering an inn.** `Actor::GetGoldAmount()` faults inside the engine on some setups;
  gold is now counted from the form the plugin resolves itself.
- **Walking through a door reset follower needs.** A cell transition briefly drops an actor from the
  high process list, which was being read as dismissal - so followers were purged and re-hired as
  fresh, zeroing their needs. In normal play this meant a follower could never actually get hungry.
- Failed purchases no longer log once every five seconds.

---

## 1.2.0

- Followers can catch **food poisoning** from raw meat, at SunHelm's own risk and rates.
- An ill follower carrying a **cure disease potion** will drink it themselves.
- Disease handling is generic - matched on spell type and effect archetype rather than any one mod's
  forms - so vanilla, Immersive Diseases and SunHelm's water-borne diseases all work.

---

## 1.1.0

- Followers **buy food and drink at inns** with their own gold, at the prices SunHelm charges you at
  the same innkeepers.
- Enough drinks across an evening and they get **drunk**, using SunHelm's own drunk effect.

---

## 1.0.0

First release.

- Independent hunger and thirst per follower, at SunHelm's own rates and thresholds.
- Followers feed themselves from their own inventory through the engine's real consumption path.
- SunHelm's own stage abilities applied to followers, so a starving companion is genuinely weakened.
- Fatigue and cold mirrored from the player.
- Needs persist in the save, including across a full restart.
- Automatic follower detection, with or without a multi-follower framework.
