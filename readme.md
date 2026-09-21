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

**They buy their own food and drink at inns.** Inside an inn, a hungry or thirsty follower spends
their *own* gold on a meal, water or ale, at the prices SunHelm charges you at the same innkeepers.
Most followers carry very little coin, so fund them if you want them self-sufficient on the road -
and they'll occasionally buy a drink in a tavern simply because they're in one. Enough of those and
they'll get drunk, using SunHelm's own drunk effect.

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

Without SKSE Menu Framework the plugin runs perfectly well on its defaults - you simply can't change
them in game. Settings can still be edited directly in the JSON file.

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

1. Install with a mod manager, or extract into `Data/`.
2. **Enable `SunHelmFollowerNeeds.esp`** in your plugin list. Without it the mod still runs, but
   cannot save follower needs between sessions - and it says so in the log.

That's it. Followers are detected automatically when recruited.

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
| Wait between meals | 0.25 hours | Game time before they eat or drink again. Zero removes the limit |
| Buy food and drink at inns | On | Spends their own gold, at SunHelm's own prices |
| Buy before raiding their own pack | Off | On means they'd rather spend coin than eat what they carry |
| Buy ale | On | The cheap answer to thirst, and what people do in taverns |
| Ale can make them drunk | On | Uses SunHelm's own drunk effect and its own drink count |
| Wait between purchases | 1 hour | Stops them emptying their purse into a bag of bread |
| Chance of a social drink | 25% | How often they buy a drink they don't actually need |
| Announcements | On | Per need, plus eating and drinking |

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
- **No eating or drinking animation.** The item is consumed for real and its effects apply, but the
  follower doesn't visibly eat or drink - food simply disappears from their pack. Animation mods such
  as Eating Animations and Sounds hook `OnObjectEquipped` on a quest alias holding the *player*, so
  nothing is listening when an NPC consumes something. Making followers animate needs work this mod
  doesn't do yet.
- **Humanoid followers only.** Animal companions are not tracked.
- **Food must be something SunHelm recognises.** Unrecognised food doesn't feed followers - but it
  doesn't feed the player either, so this fails in the same direction SunHelm does.
- **Ten followers maximum**, regardless of what your follower framework permits.

---

## Planned

Not promises, and not in any particular order - this is what the mod is likely to grow next.

**Eating and drinking animations for followers.** Currently food is consumed silently. Doing this
properly means driving the follower's behaviour graph directly, since the existing animation mods are
scoped to the player, and deciding what should happen when a follower needs to eat while following
you on the road.

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
SunHelm Follower Needs v1.0.0.0 loaded
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
