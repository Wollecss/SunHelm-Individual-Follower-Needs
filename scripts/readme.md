# Papyrus

`SunHelmFollowerNeeds.psc` declares the native functions this plugin registers. It contains no
logic, runs on no timer, and is not involved in tracking needs - the mod is still native C++ from
end to end. The script exists only so Papyrus has something to bind the natives to, and it is here
for the benefit of *other* mods, chiefly the CHIM bridge.

Both the source and the compiled `.pex` are committed. The `.pex` is a build artefact, but a small
and rarely-changing one, and shipping it means installing the mod does not require a Papyrus
compiler.

## Recompiling

Only needed if you change a function signature.

```sh
Caprica.exe scripts/SunHelmFollowerNeeds.psc \
  --game skyrim \
  -f <path to>/TESV_Papyrus_Flags.flg \
  -o scripts \
  -i scripts;<path to>/VanillaScripts
```

The script must stay marked `Native`, or Caprica rejects the native declarations.

## Why the natives look the way they do

Three calls rather than one that returns everything: Papyrus cannot return a struct, and splitting a
delimited string in script is slower and easier to get wrong than asking twice. `PeekPendingActor`
and `ConsumePending` are separate so a consumer that fails midway loses one event rather than
silently dropping it.

If the DLL is missing, the calls return `0` and `""`. A consumer degrades to doing nothing rather
than erroring, which is what lets the CHIM bridge treat this mod as optional.
