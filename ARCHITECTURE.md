# Architecture

How this plugin works internally, and why it's built the way it is. If you're changing code, read
[AGENTS.md](AGENTS.md) too - it lists invariants that are easy to break without noticing.

---

## Principle: SunHelm owns the rules, this plugin owns the followers

Nothing about hunger thresholds, accumulation rates, food categories or stage penalties is defined
here. All of it is read from SunHelm at runtime. This plugin only answers a different question:
*which actors are tracked, and what are their values?*

Practically that means:

- Rates come from `_SHHungerRate` / `_SHThirstRate`, read **live on every tick**, never cached. If a
  player changes SunHelm's MCM mid-session, followers change with it.
- Stage thresholds and restore amounts are transcribed from SunHelm's own scripts (they live as
  script properties, not GlobalVariables, so they can't be read at runtime).
- Food classification uses SunHelm's keywords and FormLists, so compatibility patches and the
  player's own food categorisations apply to followers automatically.
- Penalties are SunHelm's own stage abilities, applied to the follower.

The result is that this plugin has no opinion on balance, and inherits SunHelm's.

---

## No Papyrus, anywhere

There is no quest, no script, no `.pex`. Everything runs in native C++:

| Concern | Native mechanism |
| --- | --- |
| Follower detection | `Actor::IsPlayerTeammate()` over `ProcessLists::highActorHandles` |
| Consumption | `ActorEquipManager::EquipObject()` - the engine's real "use item" path |
| Penalties | `Actor::AddSpell()` / `RemoveSpell()` with SunHelm's ability forms |
| Notifications | `RE::DebugNotification()` |
| Persistence | `GlobalVariable` values, which the engine serialises into saves |

Follower detection uses `IsPlayerTeammate()` rather than faction membership deliberately: it's what
vanilla follow packages set, and therefore what multi-follower frameworks like NFF build on. It
requires no faction FormIDs and works the same with or without such a framework installed.

Consumption goes through `EquipObject` rather than a hand-rolled "remove item and play an idle"
sequence because it *is* the path the engine uses when any actor uses a potion. That gets item
removal, effect application and the correct keyword-conditioned animation (including animation
replacers) for free, instead of reimplementing each.

---

## Form resolution: EditorID where possible, FormID where necessary

Some form types keep their EditorID at runtime and some don't. This matters for robustness.

| Type | Keeps EditorID? | How it's resolved |
| --- | --- | --- |
| `TESGlobal` | Yes (`formEditorID` member) | By EditorID scan |
| `BGSKeyword` | Yes | By EditorID scan |
| `BGSListForm` | **No** | By FormID + plugin name |
| `SpellItem` | **No** | By FormID + plugin name |

EditorID resolution survives SunHelm renumbering its records, so globals and keywords are matched by
name. It also matters for SunHelm's food keywords specifically, which are **injected into
`Update.esm`** rather than living in `SunHelmSurvival.esp` - matching by name sidesteps caring which
file they came from.

FormLists and spells must go by FormID. Those IDs were read out of `SunHelmSurvival.esp` with
Spriggit, not copied from documentation, and cross-checked against the values SunHelm's own CHIM
bridge hardcodes. Every lookup is verified and logs an error on failure - a null FormList silently
turns every membership test into "no", which is indistinguishable from an item simply not being
food.

The 24 stage abilities are mapped by their **display name** ("Hunger: Peckish"), not by FormID
order - SunHelm's fatigue set is not in FormID order, so inferring from sequence would silently
apply the wrong penalty.

---

## Persistence: a bank of GlobalVariables

`SunHelmFollowerNeeds.esp` contains **41 GlobalVariable records and nothing else**. No quest, no
aliases, no script attachments - therefore no VMAD anywhere in the plugin.

```
_SHFN_SchemaVersion
_SHFN_Slot{0..9}_RefLo      low 16 bits of the tracked actor's FormID
_SHFN_Slot{0..9}_RefHi      high 16 bits
_SHFN_Slot{0..9}_Hunger
_SHFN_Slot{0..9}_Thirst
```

The engine serialises changed GlobalVariables into the save automatically - the same mechanism
SunHelm relies on for the player's own needs. That gives engine-managed persistence, visible to
tools like ReSaver, with no bespoke serialisation format to maintain.

**Why the FormID is split across two floats.** A `float32` has a 24-bit mantissa, so it represents
integers exactly only up to 2^24. A full 32-bit FormID would lose precision - and mod-added forms
live precisely in the range that breaks. Two 16-bit halves are each exactly representable, and
recombine as `(hi << 16) | lo`. Verified against an ESL follower's FormID (`FEEE980E`).

**Why writes call `AddChange`.** Skyrim only writes a form into a save's ChangeForms if it considers
it dirty. Assigning `TESGlobal::value` alone may not mark it, so writes also call
`AddChange(0x01)` - the GLOB value-changed flag. Confirmed working across a full process restart.

**Why this instead of a quest with reference aliases.** That was the original design. It was
abandoned because Mutagen/Spriggit throws parsing the VMAD of a real, working quest, making a
script-attached quest impossible to author reliably without a Creation Kit - and there isn't one on
the development machine. GlobalVariables provide the same engine-managed persistence with none of
that risk, and remove the Papyrus footprint entirely. `GLOB` is also the one record type verified to
round-trip byte-identically through Spriggit.

**Restoration matches on stored FormID, not slot index**, so slots shifting as the party changes is
harmless.

---

## The tick

A background thread wakes every `pollSeconds` (default 5) and queues **one** task onto the main
thread. Everything that touches the engine happens there.

```
poll thread ──queue──> main thread:
                         Followers::RefreshOnMainThread()   detect hires/dismissals
                         Needs::Update()                    accumulate, mirror, feed, persist
```

Each tick, per tracked follower:

1. Compute elapsed game time since that follower's last tick.
2. If active and not paused, accumulate hunger/thirst at SunHelm's live rates.
3. Attempt eating and drinking (subject to cooldown).
4. Resolve the stage for all four needs - own values for hunger/thirst, the player's for
   fatigue/cold.
5. Announce stage crossings, and sync the stage ability if it changed.
6. Mirror the whole tracked set into the storage globals.

**Parked followers freeze.** A follower who is waiting somewhere, or not 3D-loaded, doesn't
accumulate. They can't be fed while away, so letting them decay would only produce a companion who
is Starving the moment you collect them.

**Accumulation honours SunHelm's own pause settings** (`_SHPauseNeedsCombat`,
`_SHPauseNeedsDialogue`), so followers behave the way the player does.

---

## Feeding

When a follower crosses the configured stage threshold and the cooldown has elapsed:

1. Scan `Actor::GetInventory()`.
2. Classify each item through `SunHelm::Classify()`.
3. Pick the highest-restore candidate of the right kind.
4. Call `ActorEquipManager::EquipObject()`.
5. Apply SunHelm's restore amount for that food kind; soup also relieves thirst.

**Classification order matters, and is the subtlest part of this plugin.** SunHelm's
`_SHFoodIgnoreKeyword` and `_SHFoodIgnoreList` mean *"don't categorise this as food"* - **not**
"ignore this item". SunHelm's own eat-detection deliberately files every drink there, because drinks
belong to its separate drink-detection script. The list therefore ships already containing both
water bottles and all three waterskins.

Testing that list before drinks would make **every water source in the game** unclassifiable, and
followers would eat happily but never drink. Drinks are resolved first for exactly this reason. Salt
water is rejected before everything, since SunHelm makes it *raise* thirst.

**Guards.** A follower won't eat in combat, while being spoken to by the player, or while parked or
unloaded.

**Cooldown.** 0.25 game-hours per follower, tracked separately for food and drink so a meal doesn't
block washing it down. Without it, consumption is attempted every tick and a Ravenous follower
empties their pack in seconds.

---

## Threading rules

- The poll thread **never** touches the engine. It only queues work.
- Only **one** tick is ever outstanding. The main thread stops consuming tasks whenever the game is
  paused, in a menu or loading; without this guard the queue builds up and flushes all at once.
- `Needs::Update()` refuses to run unless the game is ready. Between `kPreLoadGame` (which clears the
  roster) and the save going live, a tick would otherwise see an empty roster and zero every storage
  slot - destroying the very data the incoming save is about to be read for.
- DevBench tool handlers run on DevBench's listener thread and marshal to the main thread through a
  blocking hand-off. See AGENTS.md for the use-after-free that lurks there.

---

## Optional DevBench integration

If [DevBench](https://github.com/ozooma10/DevBench) is installed, three tools are registered:

| Tool | Purpose |
| --- | --- |
| `sunhelm_followers.status` | Read-only snapshot: tracked followers, their values and stages, the abilities actually on them, plus the player's live SunHelm values |
| `sunhelm_followers.set_need` | Testing: overwrite a follower's hunger/thirst without waiting on game time |
| `sunhelm_followers.force_tick` | Testing: run one tick immediately instead of waiting for the poll |

`status` reports abilities by reading the actor's real spell list rather than internal bookkeeping,
so the two disagreeing is visible rather than assumed away.

When DevBench is absent the entire cost is one `Dispatch` to a plugin nobody is listening for, a null
check and a log line, once at `kPostLoad`.

---

## Regenerating the ESP

The ESP is built from YAML, so it's reviewable in version control rather than an opaque binary:

```bash
esp/gen_esp_yaml.sh esp/yaml
spriggit deserialize --InputPath esp/yaml --OutputPath esp/SunHelmFollowerNeeds.esp \
  --PackageName Spriggit.Yaml.Skyrim --PackageVersion 0.41.0
```

Note `deserialize` takes **no** `--GameRelease` - it reads that from the YAML metadata, unlike
`serialize`, which requires it.

`Settings::kMaxSlots` **must** equal `SLOTS` in `gen_esp_yaml.sh`. If they disagree,
`Persistence::Resolve()` fails and disables persistence entirely (loudly, in the log).

The plugin is ESL-flagged (`Flags: [Small]` in the YAML header). This is only safe because every
record sits in `0x800-0xFFF` and because globals are resolved by EditorID - flagging changes their
runtime FormIDs to `FExxx800`. **Changing the flag orphans needs stored in existing saves.**
