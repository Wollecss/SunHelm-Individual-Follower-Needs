# SunHelm - Individual Follower Needs

**Your followers get hungry and thirsty on their own, and do something about it.**

SunHelm Survival tracks hunger, thirst, fatigue and cold for the player. This SKSE plugin adds
**per-follower hunger and thirst** - tracked independently for each companion, using SunHelm's own
rates and thresholds - and lets followers eat and drink from their own inventory when they need to.

Fatigue and cold are deliberately *mirrored* from the player rather than simulated separately, so
sleeping at an inn or warming up by a fire still takes care of the whole party at once.

Written entirely in native C++ against CommonLibSSE-NG. **No Papyrus scripts, no quest, no script
lag.**

---

## Key features

**Independent hunger and thirst per follower.** Each companion has their own values, accumulating at
exactly the rate configured in SunHelm's own MCM. Change SunHelm's difficulty and followers follow
suit - there is no duplicate rate setting to keep in sync.

**Followers feed themselves.** When a follower gets hungry or thirsty enough, they search their own
inventory for something SunHelm recognises as food or drink and consume it through the engine's real
consumption path - the item is genuinely used up and its effects really apply. A cooldown stops
them working through an entire pack in one go.

**And you can watch them do it.** With [Eating Animations and Sounds][eas] installed, a follower
eating or drinking plays the same animation the player gets, for the ~115 items that mod covers. It
is detected rather than configured: no patch, no load-order requirement, and nothing to set up.

[eas]: https://www.nexusmods.com/skyrimspecialedition/mods/42602

**They buy their own food and drink at inns.** Inside an inn, a hungry or thirsty follower spends
their *own* gold on a meal, water or ale, at the prices SunHelm charges you at the same innkeepers.
Most followers carry very little coin, so fund them if you want them self-sufficient on the road.

**And they drink like people in a tavern.** A thirsty follower at a bar usually orders ale rather
than water, because nobody walks into an inn and asks for water - and ale quenches half as much, so
it's a real choice rather than a free one. They'll have one for the company too, not only for the
thirst. Enough across an evening and they're drunk, using SunHelm's own drunk effect, and they carry
on ordering like anyone else would. Walk out and it wears off on the way home.

Drinking themselves under the table on their own coin is the slow, cheap route - a round every couple
of hours, a few gold an hour. If you *want* a follower drunk, give them drinks. Every part of this is
on a slider, from how often they order to how long it lasts.

**They can get sick.** Once Ravenous, a follower will eat raw meat rather than starve, and runs
SunHelm's own risk of food poisoning for it. An ill follower carrying a cure disease potion will
drink it themselves - so a few potions in their pack is real insurance.

**Fatigue and cold mirror the player.** A follower is as tired and as cold as you are. This is a
design decision rather than a limitation: the player's existing survival routine covers the whole
party without micromanagement.

**The same penalties SunHelm gives you.** Followers receive SunHelm's own stage abilities at the
same thresholds, so a starving companion is genuinely weakened. Fully toggleable if you want
tracking without mechanical consequences.

**Needs persist.** Stored in the save and restored on load, including across a full game restart.

**Automatic follower detection.** No spell to cast, no dialogue, no per-follower setup. Works with or
without a multi-follower framework such as Nether's Follower Framework.

---

## Directory structure

Installed mod:

```
SunHelm - Individual Follower Needs/
├── SKSE/Plugins/
│   └── SunHelmFollowerNeeds.dll     the plugin itself
├── SunHelmFollowerNeeds.esp         ESL-flagged; persistent storage only
└── readme.md
```

Source repository:

```
.
├── src/                    plugin source
│   ├── plugin.cpp          entry point, SKSE lifecycle
│   ├── SunHelm.cpp/.h      integration layer over SunHelm's forms and rules
│   ├── Followers.cpp/.h    tracked-follower registry, hire/dismiss detection
│   ├── Needs.cpp/.h        the tick: accumulation, mirroring, stage abilities
│   ├── Feeding.cpp/.h      inventory search and consumption
│   ├── Persistence.cpp/.h  save/load through the storage globals
│   ├── Settings.h/.cpp     configuration and its JSON file
│   ├── UI.cpp/.h           SKSE Menu Framework pages
│   ├── DevBenchTools.cpp   optional live diagnostics
│   └── DevBench/           vendored MIT-licensed DevBench API
├── esp/
│   ├── gen_esp_yaml.sh     regenerates the plugin's YAML source
│   ├── yaml/               Spriggit source of truth for the ESP
│   └── SunHelmFollowerNeeds.esp
├── include/                vendored third-party headers
├── ARCHITECTURE.md         how it works internally
├── AGENTS.md               guidance for AI coding agents
└── CONTRIBUTING.md         build and test instructions
```

---

## Requirements

| Requirement | Notes |
| --- | --- |
| **SKSE64** | Required |
| **SunHelm Survival** | Required - all rates, thresholds and food categories come from it |
| **SunHelmFollowerNeeds.esp** | Must be enabled. ESL-flagged, so it costs no load order slot |
| **SKSE Menu Framework** | *Optional.* Only needed for the in-game settings menu |
| **Eating Animations and Sounds** | *Optional.* Only for the eating and drinking animations |

Without SKSE Menu Framework the plugin runs perfectly well on its defaults - you simply can't change
them in game. Settings can still be edited directly in the JSON file.

Eating Animations and Sounds is detected, not configured. Install it and followers animate; leave it
out and they consume silently. Nothing else changes either way.

### Compatibility

**SunHelm compatibility patches work automatically.** Food is identified by SunHelm's own keywords
and FormLists rather than a fixed list of items, so any patch that tags another mod's food - whether
by ESP edit or KID - is picked up with no extra work.

**Your own categorisations carry over.** When SunHelm asks you to categorise an unknown food, your
answer is stored in SunHelm's FormLists, which this plugin reads. Anything you've classified becomes
edible for your followers too.

**Safe alongside "Sunhelm - Follower Needs and Needs Based Fast Travel"**, which does something
different: it scales *your* food's value by party size and mirrors your stage onto followers
cosmetically. This mod gives each follower their own real, independent hunger and thirst.

---

## Installation

1. Download the archive from [Releases](../../releases).
2. Install it with Mod Organizer 2 or Vortex exactly as it comes - the archive is already laid out
   the way a mod manager expects. Or extract it into `Data/` by hand.
3. **Enable `SunHelmFollowerNeeds.esp`** in your plugin list. Without it the mod still runs, but
   cannot save follower needs between sessions - and it says so in the log.

That's it. Followers are detected automatically when recruited, with no spell to cast and nothing to
configure first.

**Updating** is a straight overwrite. Settings live in a JSON file that is read at startup and any
new options simply appear at their defaults, so nothing needs resetting. Needs are stored in the
save, not the file, and survive the swap.

**Uninstalling** mid-playthrough is safe, with one thing worth doing first. The mod adds no scripts
and no quest, so removing it leaves nothing running - only some unread global variables in the save.

But a follower carrying a SunHelm stage ability keeps it, and with the mod gone nothing will ever
take it off again: SunHelm manages those on the player, not on your companions. **Dismiss your
followers before uninstalling** and the mod strips their abilities on the way out. If you forget, the
ability can still be removed from the console with `player.removespell`, or by reinstalling, hiring
them, and dismissing them properly.

---

## Configuration

Found under **SunHelm Follower Needs** in the SKSE menu. All settings are written to
`Data/SKSE/Plugins/SunHelmFollowerNeeds.json` and reloaded at startup.

| Setting | Default | Description |
| --- | --- | --- |
| Track follower needs | On | Master switch |
| Followers tracked | 3 | How many at once, up to 10 |
| Hunger / Thirst | On | Tracked independently per follower |
| Fatigue / Cold | On | Mirrored from the player |
| Apply penalties to followers | On | Off keeps tracking but removes mechanical effects |
| Needs can damage health | **Off** | Opt-in. Losing a companion to starvation is a harsh failure |
| Followers feed themselves | On | Off means they only improve when fed by the player |
| Eat / drink once they are | Peckish / Thirsty | How bad it gets before they act |
| Eat raw food when desperate | On | Only once Ravenous, at SunHelm's risk of food poisoning |
| Drink their own cure potions | On | An ill follower uses a cure potion you gave them |
| Animate eating and drinking | On | Needs Eating Animations and Sounds. Inert without it |
| Wait between meals | 0.25 hours | Game time before they eat or drink again. Zero removes the limit |
| Buy food and drink at inns | On | Spends their own gold, at SunHelm's own prices |
| Buy before raiding their own pack | Off | On means they'd rather spend coin than eat what they carry |
| Buy ale | On | What people order in taverns, and a cheap answer to thirst |
| Ale can make them drunk | On | Uses SunHelm's own drunk effect and its own drink count |
| Wait between purchases | 1 hour | Stops them emptying their purse into a bag of bread |
| Time between rounds | 2 hours | How often they order another drink - see below |
| Chance of ordering ale | 65% | What a thirsty follower asks for at the bar, rather than water |
| Chance of a social drink | 25% | How often they buy a drink they don't actually need |
| Drinks stack for | 12 hours | How long a drink keeps counting towards getting drunk |
| Sober up after leaving | 4 hours | How long it lasts once they're out of the tavern |
| Announcements | On | Per need, plus eating and drinking |

### Tuning a night at the inn

**"Time between rounds" is what a tavern evening costs.** A follower keeps ordering for as long as
they're in there, so this setting, not the price of ale, decides how much of their purse an evening
takes. At the default of two hours it's a few gold an hour - ambience. Drop it to half an hour and
they'll drink themselves under the table in a fraction of the time, and pay for the privilege.

Getting drunk on their own coin is deliberately the slow route: three drinks two hours apart is a
proper night out. **If you just want a follower drunk, give them drinks** - in a tavern they'll work
through what you hand them far faster than they'd ever buy it.

**Keep "drinks stack for" above roughly three times the gap between rounds.** Drinks stop counting
once the window lapses, so if rounds are further apart than the window allows for three of them, the
count resets before they ever reach the third and they can never get drunk by buying. That's a valid
setup if you want followers who simply never overdo it - just know it's what you've chosen.

**They stay drunk while they're still inside**, however long that is, because they're still ordering.
Walking out starts the sober-up timer. Set it to zero and they're sober the moment they step outside.

The tracking limit defaults to 3 deliberately. Raising it is safe - tracking runs natively rather
than in Papyrus, so there is no script lag - it simply means slightly more data in your save per
tracked follower. Ten is the hard ceiling, set by the number of storage slots in the ESP.

---

## Known limitations

- **Outside an inn, followers only consume what they're carrying.** They cannot drink from rivers,
  wells or fountains the way the player can. Give them waterskins or bottled water for the road -
  inns are the one place they can supply themselves.
- **Buying needs gold in their pocket, not yours.** Most vanilla followers carry almost nothing, so
  this stays invisible until you fund them. The log says explicitly when one wanted a meal and
  couldn't afford it.
- **Animations need Eating Animations and Sounds.** With it installed followers eat and drink with
  the same animations the player gets, across the ~115 items it covers. Without it - or for anything
  outside its coverage - the item is still consumed for real and its effects still apply, it simply
  disappears from their pack.
- **Humanoid followers only.** Animal companions are not tracked.
- **Food must be something SunHelm recognises.** Unrecognised food doesn't feed followers - but it
  doesn't feed the player either, so this fails in the same direction SunHelm does.
- **Ten followers maximum**, regardless of what your follower framework permits.

---

## Planned

Not promises, and not in any particular order - this is what the mod is likely to grow next.

**Being cured by a healer.** Temple priests and apothecaries would offer to cure your followers'
diseases through a dialogue option, paid for by the player, rather than every follower needing to
carry their own potions.

**An optional Immersive Diseases patch,** covering both the combat and the consumption infection
routes, toggleable, with an adjustable probability.

---

## Verification and troubleshooting

The log lives at:

```
Documents\My Games\Skyrim Special Edition\SKSE\SunHelmFollowerNeeds.log
```

It is deliberately talkative. It records which followers are tracked, what they ate or drank and for
how much, when they couldn't find anything, and what was restored from a save. If a follower isn't
eating something you expect, the log says so explicitly rather than failing silently.

A healthy startup looks like:

```
SunHelm Follower Needs v1.3.0.0 loaded
Settings loaded (tracking up to 3 follower(s))
SunHelm resolved (forms found; values not read until the first tick)
Persistence ready (10 slots)
Follower poll loop started
Tracking follower 'Lydia' in slot 1
```

If **DevBench** is installed, three diagnostic tools are registered for live inspection - see
[ARCHITECTURE.md](ARCHITECTURE.md). They are inert when DevBench is absent.

---

## License

MIT. See [LICENSE](LICENSE).

The vendored DevBench API in `src/DevBench/` and the SKSE Menu Framework header in `include/` are
third-party components under their own licenses; see the notices in those files.
