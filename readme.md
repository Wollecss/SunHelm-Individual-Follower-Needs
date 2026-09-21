# SunHelm - Individual Follower Needs

Your followers get hungry and thirsty on their own, and do something about it.

SunHelm tracks survival needs for the player. This adds **per-follower hunger and thirst**, tracked
separately for each companion, and lets them eat and drink from their own inventory when they need
to. Fatigue and cold are shared with you rather than tracked separately, so sleeping and staying
warm still takes care of the whole party at once.

## Requirements

- **SKSE64**
- **SunHelm Survival** - all rates, thresholds and food categories come from SunHelm itself
- **SunHelmFollowerNeeds.esp** must be enabled (it's ESL-flagged, so it costs no load order slot)
- *Optional:* **SKSE Menu Framework** - only needed for the settings menu. Without it the mod runs
  perfectly well on its defaults, you just can't change them in game.

## How it works

Followers are picked up automatically when you recruit them - no spell to cast, no dialogue, no
configuration. Works with or without a multi-follower framework such as Nether's Follower Framework.

- **Hunger and thirst** build at exactly the rate you've set in SunHelm's own MCM. Change SunHelm's
  difficulty and your followers follow suit; there's no duplicate setting to keep in sync.
- **Eating and drinking** happens on its own. When a follower gets hungry or thirsty enough, they
  look through their own pack for something suitable and consume it - the item is really used up,
  and they'll play the matching animation. Give them food and they'll look after themselves. Don't,
  and they'll tell you they've got nothing.
- **Fatigue and cold mirror yours.** A follower is as tired and as cold as you are, so sleeping in
  an inn or warming up at a fire fixes the whole party.
- **Penalties** are the same ones SunHelm applies to you, at the same stages.
- **Needs are saved** with your game and come back when you load.

Hiring a follower starts them fresh, and dismissing one stops tracking them entirely. A follower
who's waiting somewhere for you is frozen rather than quietly starving while you're away.

## Settings

Found under **SunHelm Follower Needs** in the SKSE menu. Everything is saved to
`Data/SKSE/Plugins/SunHelmFollowerNeeds.json`.

| Setting | Default | What it does |
| --- | --- | --- |
| Track follower needs | On | Master switch |
| Followers tracked | 3 | How many at once, up to 10 |
| Hunger / Thirst | On | Tracked per follower |
| Fatigue / Cold | On | Mirrored from you |
| Apply penalties to followers | On | Off means needs are tracked but carry no mechanical effect |
| Needs can damage health | **Off** | Losing a companion to starvation is a harsh failure, so this is opt-in |
| Followers feed themselves | On | Off means they only improve when you feed them |
| Eat / drink once they are | Peckish / Thirsty | How bad it gets before they act |
| Announcements | On | Per need, plus eating and drinking |

The tracking limit defaults to 3 deliberately. Raising it is fine - tracking runs natively rather
than in Papyrus, so there's no script lag - it just means slightly more data in your save per
follower.

## Known limitations

- **Followers only drink from their own inventory.** They can't drink from rivers, wells or
  fountains the way you can. Hand them waterskins or bottled water.
- **Humanoid followers only.** Animal companions aren't tracked.
- **Food has to be something SunHelm recognises.** Anything with a SunHelm patch works
  automatically, and so does anything you've already categorised yourself when SunHelm asked you
  about an unknown food - your answer applies to your followers too. Unrecognised food doesn't feed
  followers, but it doesn't feed you either.
- **Ten followers maximum**, regardless of what your follower framework allows.

## Compatibility

Works alongside SunHelm's compatibility patches with no extra work, because food is identified by
SunHelm's own keywords and lists rather than a fixed list of items.

Safe to use with **Sunhelm - Follower Needs and Needs Based Fast Travel**, which does something
different (it scales *your* food's value by party size and mirrors your stage onto followers for
show). This mod gives each follower their own real hunger and thirst.

## Troubleshooting

The log is at
`Documents\My Games\Skyrim Special Edition\SKSE\SunHelmFollowerNeeds.log`, and it's fairly talkative
- it records who's being tracked, what they ate and for how much, and when they couldn't find
anything. If a follower isn't eating something you expect them to, that log will say so.

If nothing is tracked at all, check that `SunHelmFollowerNeeds.esp` is actually enabled - without it
the mod still runs but can't save needs between sessions, and it says so in the log.
