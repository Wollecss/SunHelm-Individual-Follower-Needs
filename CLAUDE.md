# SunHelm Individual Follower Needs

## What this is
An SKSE plugin that tracks hunger/thirst independently per humanoid follower (not just the
player), with fatigue/cold *mirrored* from the player rather than independently tracked. Ships as
MO2 mod **`SunHelm - Individual Follower Needs`**. Depends on SunHelm Survival
(`SunHelmSurvival.esp`) for all thresholds/rates/food classification, and optionally on
**SKSE Menu Framework 3** (soft dependency - runs fine without it, just no config UI).

Sibling project: `H:\Nolvus Awakening\PROJECTS\MySkyrimPlugin` (CHIM - OSLAroused Bridge). Same
author, same machine, same CommonLibSSE-NG toolchain. Read that project's own CLAUDE.md for
environment gotchas (UNC paths, `cmd.exe` via Bash no-ops, etc.) - they all apply here too.

## Prior art already ruled out (don't rediscover these)
- **`Sunhelm - Follower Needs and Needs Based Fast Travel`** (the "FET" addon) does **not** track
  per-follower needs. Every accumulate/decay call in its scripts (`_SHHungerSystem`,
  `_SHThirstSystem`, `_SHFatigueSystem`) still operates on a single hardcoded `Player` variable.
  What it actually does: divides the *player's own* restore amount by follower count (a shared-
  rations mechanic), and cosmetically mirrors the player's stage onto followers via buff/debuff
  spells + grouped chat messages + synced eat/drink animations. There is no independent follower
  state anywhere in it. This plugin replaces that with real independent tracking for hunger/thirst.
- **iNeed / iNeed-CHIM-Patch**: explicitly rejected by the user, one needs framework only (SunHelm).
- **CommonLibVR**: NOT needed. SKSE Menu Framework 3's header (`include/SKSEMenuFramework.h`) is a
  standalone `GetProcAddress` proxy against the framework's *runtime* DLL - no link-time dependency
  on ImGui or CommonLibVR at all. It compiles clean against plain CommonLibSSE-NG (verified: zero
  warnings once the vendored header is marked `SYSTEM` + `/external:W0`). Framework version 3.13+
  confirmed compatible.

## Architecture
Fully native C++, CommonLibSSE-NG, no Papyrus/ESP shipped **yet** (persistence is the one piece
still pending - see below). Everything else is native:
- **Hire/dismiss detection**: `Actor::IsPlayerTeammate()` polled every `pollSeconds` (default 5s)
  over `RE::ProcessLists::GetSingleton()->highActorHandles`, marshaled to the main thread via
  `SKSE::GetTaskInterface()->AddTask`. No faction FormIDs needed - this is also what NFF itself
  builds on, so it should work whether or not a multi-follower framework is installed. Humanoid
  filter is the race's `ActorTypeNPC` keyword.
- **Hunger/thirst**: independently accumulated per follower using SunHelm's own live rate globals
  (`_SHHungerRate`/`_SHThirstRate`, read every tick - NOT cached - so followers always track
  whatever pace the player has SunHelm/its MCM set to).
- **Fatigue/cold**: NOT independently tracked. Read straight off the player's own
  `_SHCurrentFatigueLevel`/`_SHCurrentColdLevel` every tick and mirrored onto followers (stage +
  optional debuff ability). This was a deliberate scope cut - the player already takes care of
  these for the whole party by sleeping/staying warm.
- **Self-feeding**: when a follower's hunger/thirst stage crosses the configured threshold
  (default: stage 2, "Peckish"/"Thirsty"), the plugin scans their own inventory
  (`Actor::GetInventory()`) for SunHelm-recognized food/drink, picks the best (highest restore)
  candidate, and calls `RE::ActorEquipManager::GetSingleton()->EquipObject(actor, item)` - the same
  native call the engine uses for ANY actor "using" a potion/food item. This applies the item's
  effect and removes it from inventory automatically; no manual `RemoveItem` or hand-rolled
  animation needed, and it should trigger the correct EAS-keyword-conditioned animation the same
  way real consumption does for the player.
- **Notifications**: `RE::DebugNotification()` (confirmed present in CommonLibSSE-NG's `Misc.h` -
  it is NOT present in CommonLibVR, which was the one real reason we'd considered CommonLibVR
  before realizing it wasn't needed at all).

## Key forms (verified via Spriggit, not guessed)
All read from `SunHelmSurvival.esp` via `spriggit serialize --InputPath ... --GameRelease SkyrimSE
--PackageName Spriggit.Yaml.Skyrim --PackageVersion 0.41.0` (the CLI needs all three of
GameRelease/PackageName/PackageVersion together or it errors). Globals and Keywords keep their
EditorID at runtime (`TESGlobal`/`BGSKeyword` both store it as a member) so `SunHelm.cpp` resolves
those by name, not FormID - survives SunHelm renumbering its own records. FormLists and Spells do
NOT keep an EditorID at runtime, so those are matched by FormID (see `SunHelm.cpp`'s
`ResolveListsAndSpells`) - the IDs there came directly from the YAML dump, cross-checked against
the existing CHIM bridge's hardcoded hunger/thirst/fatigue/cold globals (`SunHelm - CHIM AI
Bridge\Source\Scripts\SunHelmCHIMBridge.psc`) and they match exactly (`00EAAE`/`05C472`/
`021E3F`/`6A13C5`).

The 24 stage-ability spells (`_SHHunger00-04`, `_SHThirst00-05`, `_SHFatigue00-05`,
`_SHColdSpell0-5`) are mapped by their **display name** ("Hunger: Peckish" etc), not FormID order -
the fatigue set specifically is NOT in FormID order (`_SHFatigue01` has a lower FormID than
`_SHFatigue00`). Verified none of these 24 abilities have player-only conditions, so they apply to
followers fine.

SunHelm's keywords (`_SH_LightFoodKeyword` etc) live in **`Update.esm`**, not
`SunHelmSurvival.esp` - resolved by EditorID scan across all loaded keywords for that reason.

## Confirmed live in-game (via DevBench, see below) - 2026-09-20 session
- Plugin loads, resolves all four need globals, follower detection works (tracked "Stenvar" via
  `IsPlayerTeammate()` with zero faction FormIDs).
- SunHelm's live rate globals in this modlist's actual save: `_SHHungerRate=3.0`,
  `_SHThirstRate=3.0`, `_SHFatigueRate=1.0`, `_SHRateGoal=1.6` - matches the Nolvus SunHelm preset
  exactly (`SunHelm - Nolvus Settings\SunHelm\Config\Default\SunHelm.json`), NOT the ESP defaults
  of 10/10/10/1.0. Confirms reading these live (not caching at boot) was the right call.
- **Gotcha found and fixed**: `kDataLoaded` fires before save data is applied to GlobalVariables,
  so logging need values at that point prints the ESP's raw defaults (40/40/120/26), not the save's
  real values. Don't log values at `kDataLoaded`; log them on the first `Needs::Update()` tick
  instead (already fixed in `Needs.cpp`).

## DevBench testing workflow (MCP registration fails, HTTP works)
The `devbench-se` MCP server in `.mcp.json` (`http://127.0.0.1:8920/mcp`) only comes alive once
Skyrim is actually running with the DevBench mod loaded - it's not a standing service, so a fresh
session always sees ConnectionRefused at startup even though it's correctly configured. That's not
a bug: it means "Skyrim isn't running yet," not "reconfigure this."

Workaround: it's a plain HTTP JSON-RPC endpoint, so `curl` works directly without needing MCP
registration at all. Sessions expire quickly (a few minutes idle), so re-handshake on every call
rather than reusing a session ID. A working helper script pattern:
1. `POST /mcp` with `initialize` → read `Mcp-Session-Id` response header.
2. `POST /mcp` with `notifications/initialized` (same session header).
3. `POST /mcp` with `tools/call` (same session header).

DevBench also doesn't respond until the game's main thread is actually pumping tasks - right after
launch, `inspect kind=state` returns a 504 ("main-thread task did not start within 5000ms") for
maybe 30-60 seconds while the game finishes loading. Poll, don't assume failure means broken.

Useful DevBench tools for this project: `console` (with `capture:true`, then a separate `read`
call - the two are NOT combined in one call) to independently cross-check any SunHelm global via
`show <EditorID>`; `inspect kind=state` for `playerLoaded`.

This plugin also vendors its own DevBench tools (`src/DevBenchTools.cpp`, using the MIT-licensed
`src/DevBench/DevBenchAPI.h`/`.cpp` copied verbatim from the sibling project - see that file's
header comment for why it's safe to vendor into a closed-source plugin despite devbench itself
being GPL-3.0): `sunhelm_followers.status` (read-only snapshot), `sunhelm_followers.set_need`
(testing only - directly overwrite a tracked follower's hunger/thirst by name, no waiting on game
time), `sunhelm_followers.force_tick` (testing only - run one tick immediately instead of waiting
up to `pollSeconds`). The force_tick handler blocks the DevBench listener thread on a condition
variable until the main-thread task actually completes - anything touching an Actor (inventory,
equip) is main-thread-only, but DevBench's handler contract needs a synchronous JSON result, so a
blocking hand-off is the only way to satisfy both. `status`/`set_need` don't need this - they only
touch our own mutex-guarded `Followers::State` structs, never the engine directly, so they answer
straight from the listener thread.

## CURRENT STATE (2026-09-21, session 3) - read this first

**Every core feature is verified working in real gameplay.** Everything is committed; last commit
`a20f7e1`. The Quest+alias persistence plan further down was abandoned - see "Persistence, as
actually built".

Verified live (Stenvar, FormID `000B998C`):
- Follower detection, per-follower ticking at SunHelm's live rate (3.0/hr in this modlist).
- **Self-feeding**: hunger set to 222 → after a tick it read 147.045, exactly -75.0 (SunHelm's
  medium-food restore). A second attempt found nothing left, so `ActorEquipManager::EquipObject`
  both applies the effect and genuinely consumes the item.
- **All four notification paths** seen on screen: "X has nothing to eat.", "X has nothing to
  drink.", and both stage-crossing announcements.
- **Persistence**, across a full overnight process restart: values written before a save came back
  as `Read slot: 000B998C hunger=300.9 thirst=200.9`. So a raw `TESGlobal::value` write plus
  `AddChange(0x01)` *does* persist - that question is settled.
- **Debuff abilities**, read off the actor's real spell list via `SunHelm::AppliedStageOn`: with the
  follower at Ravenous/Dehydrated and the player at Chilly/Rested, the follower carried hunger 4,
  thirst 4, cold 2, fatigue 1. Confirms both that abilities land on followers and that mirrored
  needs take the *player's* stage, not the follower's.

**ONLY PENDING TEST: settings persistence** (built, deployed, untested). Launch, open
SunHelm Follower Needs → Settings, change something (e.g. Followers tracked → 5, accept the
one-time warning popup; toggle an "Announce" off), **fully quit**, relaunch. Expected: changes stuck,
and the log reads `Settings loaded (tracking up to 5 follower(s))` rather than defaulting to 3.
The JSON should appear at `Data/SKSE/Plugins/SunHelmFollowerNeeds.json`, which under MO2's VFS most
likely means `H:\Nolvus Awakening\MODS\overwrite\SKSE\Plugins\`. If it lands somewhere unexpected,
adjust `kSettingsPath` in `src/Settings.cpp`.

**Trap: a timed-out main-thread hop still runs later.** `RunOnMainThreadBlocking` in
`src/DevBenchTools.cpp` gives up after 5s, but giving up does NOT cancel the queued task - it runs
whenever the main thread next pumps. An early version had the caller pass a lambda capturing a local
`std::unordered_map` by reference; once a timeout let the caller return and destroy it, the task
wrote into freed memory. That crashed the game for real (`EXCEPTION_ACCESS_VIOLATION` reading
`0xFFFFFFFFFFFFFFFF`, all five top stack frames in our DLL, faulting instruction
`mov rbx, [rax+rcx*8+0x08]` - an unordered_map bucket lookup). Easy to hit, because the main thread
stops pumping exactly when someone is poking at a status tool: paused, in a menu, or loading.
Anything handed to that helper must own its state via `shared_ptr` and return by value - never
capture a local by reference.

**Trap: the poll loop keeps ticking *during* a save load** (cost a full test cycle once). `Needs::Update()` is gated on `Followers::IsGameReady()` precisely because, without it, a
tick fires between `kPreLoadGame` (which clears the roster) and the save going live, and
`Persistence::Save()` zeroes every storage slot before `Load()` can read it. Any new write-on-tick
work needs the same gate.

**After that, the remaining roadmap**: the CHIM bridge itself (the original point of the project,
deliberately deferred until the core worked standalone - it now does), then release polish (readme,
whether to ESL-flag the ESP, no debug residue), then creature followers (still deferred).

## Superseded plan - the Quest+alias approach, and why it was dropped
Follower state (`Followers::g_tracked`) is **pure in-memory** right now - `Followers::Reset()` is
called on `kNewGame`/`kPreLoadGame`, so every load starts every follower fresh. The design (agreed
with the user) is a thin, inert Papyrus alias quest - N `ReferenceAlias` slots with a tiny attached
script holding only hunger/thirst float properties, zero logic, no `OnUpdate` - purely so the
native plugin can read/write those properties and get the engine's own battle-tested save
serialization (and ReSaver visibility) for free, instead of hand-rolling an SKSE co-save format.

**Blocker found this session**: there is no Creation Kit on this machine. Caprica.exe
(`H:\Nolvus Awakening\TOOLS\Caprica.exe`, `--game skyrim`) is available and works for compiling the
alias script's `.psc` to `.pex`. But building the Quest+Alias **ESP itself** via Spriggit
(deserializing hand-authored YAML) hit a real bug: serializing `SunHelm - CHIM AI Bridge\
SunHelm_CHIM_Bridge.esp` (a real, working, in-game-functional quest from the sibling project) with
`spriggit serialize` throws
`Mutagen.Bethesda.Serialization.Exceptions.FilePathedException` /
`ArgumentOutOfRangeException` inside `QuestAdapterBinaryOverlay.CustomFileNameEndPos()` - Mutagen's
VMAD (script-attachment) parser chokes on a quest that demonstrably works. This was found on the
*read* path before a single byte of our own YAML was written, so there's real reason to distrust
the *write* (deserialize) path too for a Quest with an attached script - and there's no CK here to
sanity-check the result if it silently writes something subtly wrong.

**Next session should try, in order**:
1. Check for a newer `Spriggit.Yaml.Skyrim` package version (`--PackageVersion` bump) - this may
   just be a fixed bug in an old pinned version (0.41.0 was what was on hand this session).
2. If still broken, test whether a bare `Quest` record with NO VMAD/aliases round-trips fine via
   Spriggit (isolates whether the bug is specifically about VMAD or the whole Quest write path) -
   if the plain quest works, the script attachment might be addable via a different route (e.g.
   post-process the ESP with SSEEdit's scripting rather than doing it all through Spriggit).
3. Fall back to **SSEEdit's Pascal scripting** (`H:\Nolvus Awakening\TOOLS\SSE Edit\SSEEdit.exe`) -
   a legitimate, well-established alternative to CK for programmatically creating new records,
   used community-wide for exactly this kind of task. Untested this session; would need learning
   xEdit's scripting API from scratch.
4. Only as a last resort, reconsider native SKSE co-save serialization (the original alternative
   design) - it was passed over specifically because it's less safe (no ReSaver visibility, no
   engine-provided versioning) and reintroduces bespoke serialization code, but it doesn't need an
   ESP at all.

## Persistence, as actually built

The alias plan above was dropped for the GlobalVariable bank described here - simpler, needs no CK,
no Caprica, no `.pex`, and keeps the plugin fully native.

`esp/SunHelmFollowerNeeds.esp` contains **only 41 GlobalVariable records** - no quest, no aliases,
no script attachments, therefore no VMAD anywhere, which is what sidesteps the Mutagen bug. It is
reproducible: `esp/gen_esp_yaml.sh <outdir>` regenerates the YAML, then
`spriggit deserialize --InputPath <outdir> --OutputPath <esp> --PackageName Spriggit.Yaml.Skyrim
--PackageVersion 0.41.0` rebuilds the plugin (note: `deserialize` takes NO `--GameRelease`; it reads
that from the YAML metadata, unlike `serialize` which requires it). Verified to round-trip
byte-identically.

Layout: `_SHFN_SchemaVersion` plus, per slot 0-9, `_SHFN_Slot{N}_{RefLo,RefHi,Hunger,Thirst}`
(FormIDs `000800`-`000828`). A follower's FormID is split into two 16-bit halves because a float32
mantissa holds integers exactly only up to 2^24 - a whole 32-bit FormID would lose precision, each
half is lossless. `Settings::kMaxSlots` MUST equal `SLOTS` in `esp/gen_esp_yaml.sh`.

Resolved by EditorID (globals keep theirs at runtime), so renumbering or a later ESL flag is safe.
Written every tick rather than on a save hook, since the engine captures whatever a global holds
whenever the player saves. Read back at `kPostLoadGame` and staged as *pending restores* applied
when that follower is next detected - the actor usually isn't loaded yet that early, and the poll
loop would otherwise purge an unloaded entry as dismissed. Restoration matches on stored FormID, not
slot index, so slots shifting as the party changes is harmless.

**The ESP must be activated in MO2's plugins pane**, not just the mod enabled. Without it the plugin
runs fine but logs `SunHelmFollowerNeeds.esp is not loaded` and persistence sits out.

## Design decisions locked in this session (don't re-litigate without reason)
- Humanoid followers only; creatures deferred.
- Default active-tracking cap: 3, adjustable 1-10 via one MCM-style slider (not a separate
  toggle) - the ESP will eventually declare 10 alias slots regardless of the cap, since unfilled
  aliases cost ~nothing and the ceiling can't be raised at runtime once built.
- Eat/drink trigger stage: configurable, default stage 2 ("Peckish"/"Thirsty").
- Relief amounts: reused verbatim from SunHelm's own `_SHEatDetection`/`_SHDrinkDetection` (light
  40 / medium 75 / heavy 125 hunger; drink 80 / alcohol 40 / soup 20 thirst; soup also relieves
  thirst on top of hunger).
- Followers only eat/drink when: not in combat, not the actor currently in dialogue with the
  player (checked via `MenuTopicManager::speaker`), not parked/waiting (`WaitingForPlayer` AV) or
  not `Is3DLoaded()`.
- Fatigue/cold debuffs on followers: on by default, toggleable independently of tracking.
- HP damage from maxed-out needs: **off by default** (deliberately gentler than SunHelm's own
  player damage curve when enabled - a follower dying because you forgot to feed them is a harsher
  failure than the player taking the same risk themselves).
- Notifications: per-need toggle, plus a separate "no food/drink available" notification
  (rate-limited to once per stage, not once per poll) - CHIM bridging is explicitly deferred until
  the core mod works standalone.

## Build
Same pattern as the sibling project: `H:\Nolvus Awakening\PROJECTS\SunHelmFollowerNeeds\
build_plugin.bat` via the **PowerShell tool** (not Bash - `cmd.exe` from Bash silently no-ops on
this machine). Output log: `build_output.txt` in the project root. Deploys to
`H:\Nolvus Awakening\MODS\mods\SunHelm - Individual Follower Needs\SKSE\Plugins\`. The deploy copy
step fails (harmlessly - compile still succeeds) if Skyrim is currently running with the DLL
loaded; just close the game and rebuild.

`vcpkg.json` deps: `commonlibsse-ng`, `nlohmann-json` (for the vendored DevBench tools' JSON).

## Environment notes specific to this project
- This machine's Documents folder is **OneDrive-redirected**:
  `C:\Users\carvw\OneDrive\Documents\My Games\Skyrim Special Edition\SKSE\` - not the default
  `C:\Users\<user>\Documents\...`. Every other SKSE plugin's log lives there too. Don't waste time
  checking the non-redirected path first.
- Raw string literals for JSON tool descriptors need a custom delimiter (`R"json(...)json"`, not
  bare `R"(...)"`) whenever the JSON text itself might contain a literal `)"` sequence (e.g. a
  parenthetical in a description right before the closing quote) - hit this exact bug once in
  `DevBenchTools.cpp` and it produces very confusing cascading MSVC syntax errors that don't point
  at the real cause.
