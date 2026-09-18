# ShadowExtender — GTA:SA Android Mod Loader Plugin

Gives trees a real shadow by hooking `CShadows::StoreShadowForTree` —
a function that exists in the shipped game but does nothing (a NOP).
The engine already calls it once per tree; this plugin replaces it with
a real call into the built-in shadow renderer (`CShadows::StoreShadowToBeRendered`).
Configurable via `ShadowExtender.ini` — no recompiling needed to tune
opacity, shadow size, direction, or per-model blacklist.

## Status / scope

- **Trees/vegetation:** implemented, via the real `StoreShadowForTree` hook.
- **Small objects / buildings:** not yet implemented. There's no equivalent
  stub hook for these in the headers inspected so far; adding support would
  need the world/pool iteration API (`CPools`/`CWorld` equivalents), which
  hasn't been confirmed against real SDK headers yet.
- **Shadow direction:** currently fixed (set in the INI), not tied to the
  live sun position. The real mechanism for that
  (`CShadows::CalcPedShadowValues(CVector UnitVecToLight, ...)`) is known,
  but the SDK's actual light-direction source hasn't been located yet.

This all reflects headers actually pulled from the real SDK (see below),
not guesses — but it took several rounds of "paste me this header" to get
here, so scope was deliberately kept to what's verified rather than
extended into more unconfirmed territory.

## Requirements

- Android NDK (the CI workflow uses r29)
- [AndroidModLoader](https://github.com/RusJJ/AndroidModLoader) (AML) installed
  on-device
- This repo's `aml-psdk` submodule
  ([AndroidModLoader/aml-psdk](https://github.com/AndroidModLoader/aml-psdk))
- A `mod/` folder containing AML's own loader headers (`amlmod.h`, `iaml.h`,
  `interface.h`) — typically obtained from AML's `template_of_mod` starter
  project. This project's own `mod/IniConfig.h` helper lives alongside
  those in the same folder.

## Building

```bash
git clone --recursive <this-repo-url>
cd GTASA-ShadowExtender
ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk
```

Outputs land in `libs/armeabi-v7a/` and `libs/arm64-v8a/`. Rename/copy to
match AML's naming convention (e.g. `libAML_PSDK_ShadowExtender64.so` for
arm64) and push it to the game's mods folder:

```
/sdcard/Android/data/com.rockstargames.gtasa/mods/
```

If `aml-psdk` wasn't pulled (e.g. downloaded as a zip instead of cloned):

```bash
git submodule update --init --recursive
```

## Configuring

Drop `ShadowExtender.ini` (see `config/ShadowExtender.ini`) at:

```
/sdcard/Android/data/com.rockstargames.gtasa/files/ShadowExtender.ini
```

All options are documented inline: enable/disable, opacity, shadow size,
fixed shadow-spread direction, and a model-ID blacklist.

## Verifying it loaded

```
logcat -d | grep -E "ShadowExtender|AndroidModLoader"
```

You should see AML log a line like
`Mod (GUID net.psdk.samod.shadowextender) has been preprocessed.`
confirming the mod was picked up (this pattern is standard across AML mods,
confirmed from public examples).

## Extending to objects/buildings

To go beyond trees, the next things to confirm from the real SDK (the same
way the current headers were confirmed — by pulling them from your local
`aml-psdk`/`mod` checkout) are:

1. A `CPools`/`CWorld`-equivalent header, for iterating live objects/buildings
   near the camera each frame.
2. A camera-position accessor (`CCamera`-equivalent) for distance culling.
3. The real light-direction source for `CShadows::CalcPedShadowValues`, to
   make shadow spread follow time-of-day instead of a fixed direction.

Happy to extend this once those headers are available.
