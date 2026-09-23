# Contributing

Bug reports, compatibility findings and pull requests are all welcome. If you're an AI coding agent,
read [AGENTS.md](AGENTS.md) as well - it lists invariants that are easy to break silently.

---

## Reporting a bug

The log is the single most useful thing you can attach:

```
Documents\My Games\Skyrim Special Edition\SKSE\SunHelmFollowerNeeds.log
```

It records which followers are tracked, what they ate or drank and for how much, when they couldn't
find anything, and what was restored from a save. Please include the whole file rather than an
excerpt - the interesting line is often earlier than expected.

Useful to state:

- Whether `SunHelmFollowerNeeds.esp` is enabled (the log says if it isn't)
- Your SunHelm MCM rate settings, or that they're default
- Which follower framework you use, if any
- For "my follower won't eat X": the exact item name. Most such reports are the item having no
  SunHelm category, which means the *player* can't eat it for hunger either - worth checking first.

---

## Building

**Requirements**

- Visual Studio 2022 Build Tools (MSVC, C++20 or later)
- CMake 3.21+ and Ninja
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set

Dependencies come from vcpkg: `commonlibsse-ng-alandtse` and `nlohmann-json`.

**This is the Skyrim 1.7 branch.** It builds against the maintained
[CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) through an overlay port in
`vcpkg-ports/commonlibsse-ng-alandtse/`, pinned to an exact commit. No vcpkg registry serves it:
the colorglass registry `main` uses still carries CharmedBaryon's fork, which has no 1.7 support.
The port's comments explain each build option it sets - every one works around something that
broke at upstream's default.

To move to a newer CommonLibSSE-NG, change `REF` and `SHA512` in that portfile, and the
`default-registry` baseline in `vcpkg-configuration.json` to the `builtin-baseline` from upstream's
own `vcpkg.json` at that commit, so the dependencies meet its minimum versions.

**Configure and build**

```bash
cmake --preset release-1.7
cmake --build build/release-1.7 --config Release
```

The presets are named differently from `main`'s on purpose, so the two branches never share a
build folder. Their vcpkg packages are incompatible, and switching branches onto a shared folder
would quietly build against the wrong library.

Set `SKYRIM_MODS_FOLDER` to your mod manager's mods directory and the build copies the DLL straight
into `SunHelm - Individual Follower Needs/SKSE/Plugins/` on success. Alternatively set
`SKYRIM_FOLDER` to deploy into `Data/`.

**The deploy step fails while Skyrim is running**, because the game holds the DLL open. The compile
itself still succeeds - just close the game and rebuild.

---

## Changing the ESP

The plugin is generated from YAML so it stays reviewable in version control:

```bash
esp/gen_esp_yaml.sh esp/yaml
spriggit deserialize --InputPath esp/yaml --OutputPath esp/SunHelmFollowerNeeds.esp \
  --PackageName Spriggit.Yaml.Skyrim --PackageVersion 0.41.0
```

`deserialize` takes no `--GameRelease` (it reads that from the YAML); `serialize` requires it.

If you change the number of storage slots, update **both** `SLOTS` in `gen_esp_yaml.sh` and
`Settings::kMaxSlots`. They must match.

Be aware that changing the ESL flag, or the FormID range, orphans needs stored in existing saves -
they're keyed to the old runtime FormIDs.

---

## Testing a change

**Compiling is not testing.** Every significant bug in this plugin's history compiled cleanly and
looked correct on review; all of them were found by running the game and reading the log. If you
haven't run it, say so in the PR rather than implying otherwise.

A reasonable manual pass:

1. Load a save with a follower. Confirm they're picked up in the log.
2. Let hunger build, or force it if you have DevBench, and confirm eating with the expected restore
   amount.
3. Give them a drink and confirm drinking - historically the more fragile path.
4. Save, reload, confirm `Persistence::Load read N populated slot(s)` and the values coming back.
5. Dismiss and re-hire, confirming the tracker resets rather than restoring.

With [DevBench](https://github.com/ozooma10/DevBench) installed you can drive most of this directly
via `sunhelm_followers.status`, `set_need` and `force_tick` instead of waiting on game time.

### Reading a crash log

Crash Logger reports offsets into `SunHelmFollowerNeeds.dll` rather than function names. The build
writes `build/release-1.7/SunHelmFollowerNeeds.map` for exactly this: subtract the image base
(`0x180000000`) from each `Rva+Base` in that file and take the nearest symbol at or below the
reported offset.

The map has to come from **the same build as the DLL that crashed**, so copy it aside before
rebuilding - a rebuilt map silently shifts every address. Note also that Skyrim's "PROBABLE CALL
STACK" is a stack scan, not a real unwind: expect stale frames mixed in with the genuine ones. Trust
frames that form a coherent calling chain, and treat isolated ones as noise.

---

## Pull requests

- Keep the native-only architecture. Adding a Papyrus script or quest would undo a core design goal.
- Don't cache what's meant to be read live - SunHelm's rates deliberately track its MCM in real time.
- Warnings are fixed, not silenced. Vendored headers in `include/` and `src/DevBench/` stay
  byte-identical to upstream; they're marked `SYSTEM` so their warnings don't surface.
- Explain *why* in commit messages, particularly for anything ordering-sensitive. Several bugs here
  were ordering problems that looked arbitrary until explained.

---

## License

This branch is GPL-3.0-or-later; `main` is MIT. Contributions to each are accepted under that
branch's licence.

**Send features to `main`, not here.** MIT code can be merged into this branch freely, so anything
landing on `main` reaches both builds. GPL code can't go the other way without its author agreeing
to relicense it, so a feature contributed only here would be stuck on 1.7. This branch is for what
genuinely differs between the two: the library, the build setup, and anything 1.7 changed.
