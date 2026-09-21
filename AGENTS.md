# Guidance for AI coding agents

Read [ARCHITECTURE.md](ARCHITECTURE.md) first - it explains *why* things are the way they are. This
file covers what will bite you.

---

## The single most important rule

**This plugin cannot be verified by compiling it.** Every significant bug found during development
compiled cleanly, passed review, and looked correct. They were found by running the game and reading
the log:

| Bug | Why review missed it |
| --- | --- |
| Storage globals wiped during load | Needed the precise window between `kPreLoadGame` and the save going live |
| Use-after-free crash | Only when a 5s timeout fired, i.e. only while the game was paused |
| Followers ate 4 meals in a minute | Needed a hungry follower, a stocked pack, and a menu stalling the main thread |
| Followers could never drink at all | Required reading a helper property in SunHelm's source that looked irrelevant |

If you change behaviour, say plainly that it is unverified until someone has run it. Do not describe
compiling as testing.

---

## Invariants - break these and it fails silently

**1. The poll thread never touches the engine.** It queues work; the main thread does it. Reading
`ProcessLists`, actor inventories, or the Papyrus VM off the main thread races the game mutating
them.

**2. Only one tick may be outstanding.** `g_tickQueued` guards this. The main thread stops consuming
tasks whenever the game is paused, in a menu, or loading - and the poll thread keeps waking. Remove
the guard and tasks pile up, then flush together, producing bursts of whatever the tick does.

**3. Nothing may write storage while the game isn't ready.** `Needs::Update()` checks
`Followers::IsGameReady()`. Between `kPreLoadGame` (which clears the roster) and the save going live,
a tick would see an empty roster and zero every storage slot - destroying exactly the data the
incoming save is about to be read for. Any new write-on-tick work needs the same gate.

**4. Work handed to `RunOnMainThreadBlocking` must own its state.** Timing out does **not** cancel
the queued task; it runs whenever the main thread next pumps. An earlier version passed a lambda
capturing a local `std::unordered_map` by reference, and once a timeout let the caller return and
destroy it, the task wrote into freed memory. That crashed the game
(`EXCEPTION_ACCESS_VIOLATION` reading `0xFFFFFFFFFFFFFFFF`, faulting on an unordered_map bucket
lookup). Use `shared_ptr`-owned state and return by value. Never capture a local by reference.

**5. Never call `RE::Actor::GetGoldAmount()`. Use `SunHelm::GoldAmount()`.** `GetGoldAmount()`
resolves the gold form through `BGSDefaultObjectManager::GetObject(kGold)`, which indexes a parallel
`objectInit[]` bool array off the manager singleton. On this setup that read lands outside the
process and faults *inside the engine*, so nothing on our side can guard it. It crashed the game
three times. `SunHelm::GoldAmount()` counts the form we resolve ourselves, which also guarantees the
affordability check and `RemoveItem` agree on what gold is.

The wider lesson: a CommonLibSSE-NG call is not automatically safe. This header carries three
different default-object table sizes for SE, AE and VR. When an engine helper crashes, prefer a form
you resolved yourself over the library's convenience wrapper.

**6. Diagnostic code gets the same actor guards as game code.** `Needs::Update()` keeps every deep
engine read behind `State::active` (`Is3DLoaded()` and not waiting), but `ForEachTracked` visits
every tracked follower regardless - so a visitor that skips the gate reads state an unloaded actor
does not have. Report liveness as a field rather than skipping the follower: "not loaded" and
"nothing applied" are different answers. (This gate was added while chasing the crash above and did
not fix it - the faulting actor was loaded. It is correct defensively; it was not the bug.)

**7. Absence from the high process list is not dismissal.** `RefreshOnMainThread` walks
`ProcessLists::highActorHandles`, and a cell transition drops an actor out of it for a moment.
Erasing on absence meant walking through any door purged the follower and re-added them as a fresh
hire, zeroing needs - so in normal play a follower could never get hungry, and the mod's whole
premise quietly did nothing. Ask `IsPlayerTeammate()` instead: it is a flag on the actor, not
process state, so it answers correctly whether or not they're loaded. Keep unseen followers and mark
them inactive; only erase on a positive answer (not a teammate, dead, or disabled).

**8. `Settings::kMaxSlots` must equal `SLOTS` in `esp/gen_esp_yaml.sh`.** Otherwise
`Persistence::Resolve()` can't find every storage global and disables persistence.

**9. Don't read GlobalVariable *values* at `kDataLoaded`.** That fires before save data is applied,
so you get the ESP's compiled-in defaults. Resolving forms there is correct; reading values is not.
This produced a log line claiming hunger was `40.0` when the save actually held `111.38`.

---

## SunHelm semantics that are easy to misread

**`_SHFoodIgnoreList` / `_SHFoodIgnoreKeyword` mean "not food", not "ignore".** SunHelm files every
*drink* there on purpose, because drinks belong to its separate drink-detection script. The list
ships containing both water bottles and all three waterskins. Test it before drinks and every water
source in the game becomes unclassifiable. This bug survived several play sessions.

**SunHelm's food keywords live in `Update.esm`,** not `SunHelmSurvival.esp` - they're injected. Match
them by EditorID.

**Stage thresholds are script properties, not GlobalVariables,** so they cannot be read at runtime
and are transcribed into `SunHelm.cpp`. Rates *are* globals and must be read live, never cached.

**The stage abilities are not in FormID order.** `_SHFatigue01` has a lower FormID than
`_SHFatigue00`. They're mapped by display name for that reason. Don't "simplify" this to arithmetic.

---

## Verifying a change

1. **Build.** See [CONTRIBUTING.md](CONTRIBUTING.md).
2. **Run the game and read the log.** It's deliberately verbose - who's tracked, what was eaten and
   for how much, what was restored, and when nothing was found.
3. **Cross-check independently where possible.** During development, values were confirmed three
   ways: the plugin's own log, the ESP's compiled defaults via Spriggit, and a live console read.
   Agreement across independent sources is what caught a misleading log line.

If DevBench is installed, `sunhelm_followers.status`, `set_need` and `force_tick` let you drive
scenarios directly instead of waiting on game time. `status` reads abilities off the actor's real
spell list, so it catches internal bookkeeping disagreeing with reality.

**Silence is not evidence.** Several bugs hid because a failure path logged nothing, making "found
nothing" indistinguishable from "never ran". If you add a path that can silently do nothing, log it.

---

## Things that are deliberate, not oversights

- **Fatigue and cold are mirrored, not simulated.** Scope decision: the player's survival routine
  should cover the party.
- **Hire resets a follower; dismiss stops tracking entirely.** Deliberate simplification - tracking
  absent followers was judged not worth the complexity.
- **Followers can't drink from world water sources.** They only use their inventory. SunHelm's
  fountain patches work through activators, which is a player-only interaction.
- **Needs damage is off by default.** Losing a companion is a harsher failure than the player taking
  the same risk.
- **DevBench tools ship in release builds.** They're inert without DevBench, and `status` is real
  support value.
- **The ESP is generated from YAML**, so it's reviewable in version control rather than an opaque
  binary.

---

## Style

- Comments explain *why*, not *what*. Most code has none; the ones that exist mark a hidden
  constraint, a non-obvious ordering, or a bug that was actually hit.
- Prefer reading real headers over recalling an API. CommonLibSSE-NG headers land in
  `build/<preset>/vcpkg_installed/x64-windows-static-md/include/RE/`.
- Warnings are treated as errors to fix. Vendored third-party headers in `include/` are marked
  `SYSTEM` with `/external:W0` so upstream code isn't edited to silence them - keep it that way so
  those files stay byte-identical to upstream.
