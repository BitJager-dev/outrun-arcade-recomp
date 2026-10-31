# OutRun Online Arcade Recompiled

Static recompilation of **OutRun Online Arcade** (Xbox 360 XBLA, SEGA / Sumo Digital, 2009; Title ID 58410968, default.xex 0.0.1.5 built 2009-04-03) to native Windows, using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.9.0.

**No game data is included.** You need your own OutRun Online Arcade XBLA package (the Xbox 360 LIVE file from your
console's storage).

## Download
Get the latest `outrun_arcade-<version>-win-amd64.zip` from [Releases](https://github.com/darkuni/outrun-arcade-recomp/releases),
unzip it, put your package file (or the `58410968` folder copied from the console) in the same folder and run
`outrun_arcade.exe`: the first start makes the `assets` folder from it. Nothing else to install. Requirements: 64-bit Windows 10/11 and the
[Microsoft Visual C++ Redistributable 2015-2022 (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Building
GitHub Actions builds on every push to `main` (or run **Build Windows** by hand from the Actions tab); publishing a
release (or pushing a `v*` tag) attaches the zip to it. Unzip a build and put your extracted game files in an `assets`
folder next to the exe (`tools/stfs_extract.py <package> assets` extracts the package):

```
outrun_arcade.exe
*.dll
assets\
  default.xex
  ...
```

## In-game settings (F1 or Esc)
Same menu as the Poker Smash / ReBirth recomps: display mode (also F11), window size, render resolution, VSync, aspect ratio, texture filtering, audio buffer, keyboard on/off, per-button key rebinding, quit. Saved to `outrun_arcade.toml` next to the exe. F3 = performance overlay. Runs as the full game.

### Default keyboard layout
| Action | Controller | Key |
|---|---|---|
| Steer | Left stick | Left / Right |
| Accelerate | RT (+ stick up) | Up |
| Brake | LT (+ stick down) | Down |
| A / B / X / Y | A / B / X / Y | Z / X / A / S |
| LB / RB | LB / RB | Q / W |
| D-pad | D-pad | I / J / K / L |
| Start / Back | Start / Back | Enter / Backspace |

## Layout
| Path | What |
|---|---|
| `outrun_arcade_manifest.toml` | ReXGlue project manifest (codegen input) |
| `outrun_arcade_overrides.toml` | Pointer-only functions |
| `generated/` | Recompiled C++ output of `rexglue codegen` |
| `src/outrun_arcade_app.h` | App class - game folder default, Xenos GPU plugin, full-game license, first-launch defaults, menu/F11 binds |
| `src/game_menu.*`, `src/game_keys.h` | F1/Esc settings menu and this game's key layout |
| `tools/` | Package/XEX extraction (STFS, XEX2 basic + LZX), pointer-function scan, label check |
| `.github/workflows/build-windows.yml` | CI: LLVM + SDK (cached) + game, uploads exe + DLLs |

## Codegen fixes so far
- No analysis errors on the first pass.
- 44 functions reachable only through vtables/callbacks (`tools/scan_pointer_funcs.py`).

## Audio fixes
- **Engine pops** (SDK p22+, `xma_start_latency_us`, default 6000): a freshly
  started XMA voice gets its first audio a few ms late, like the 360's
  hardware decoder. The game starts engine voices loud and turns them down a
  frame later; with instant decode that first loud frame was audible as a pop.
- **Song-title callout cut off** (`src/callout_hold.cpp`,
  `outrun_callout_hold_ms`, default 1000): after the song pick the front end
  waits (state 62) for 75 loads, then stops all menu voices. Loads finish far
  faster than on a 360, which cut the callout mid-word; the wait is held for
  at least 1 s. `outrun_callout_trace = true` logs it to callout_trace.txt.
- **Single Player confirm sound cut off** (`src/voice_hooks.cpp`,
  `outrun_keep_confirm`, default on): the game-mode task's init stops every
  sound 16 frames after the pick; on a 360 the confirm sound survives that
  stop (measured from real hardware). Stops from that init path skip voices
  started in the last 600 ms. `outrun_voice_trace = true` writes
  voice_api_trace.txt (every voice call, with callers).

## Diagnostics
- Crash reporter: `crash-*.txt` + `.dmp` next to the exe; decode offsets with
  `outrun_arcade.map` (shipped in the build).
- SDK: `xma_trace`, `audio_dump`, `xma_dump`, `xma_mute`/`xma_solo` (clip
  names from `xma_clips.txt`).
- `tools/diagnostics/voice_trace.cpp` (not built): logs who destroys a voice.


## Shader cache
The SDK stores every shader and pipeline the game uses in `cache/shaders/shareable/` in the user data folder
(`58410968.xsh` and `58410968.fbo.vk.xpso` on Vulkan). On the next start it prepares all of them up front, so only
effects never seen before are compiled mid-race, which shows as a short hitch the first time. The files don't depend
on the GPU, so caches from several machines or play sessions can be combined:

```
python3 tools/merge_shader_cache.py <out_dir> <cache_dir> [<cache_dir> ...]
```

Each record is checked against its hash and kept once (needs the `xxhash` Python module). Copy the merged files into
`cache/shaders/shareable/` of an install before its first race. They contain the game's shader code, so they belong
with your own game files and are not committed or shipped here.

## Disclaimer
Unofficial fan project, not affiliated with or endorsed by SEGA, Sumo Digital or Microsoft. This repository and its
releases contain no game data (graphics, sound, music, tracks): those come from your own package. `generated/` and the
release exe contain code machine-translated from the game's executable. The project's own code is under the
[MIT License](LICENSE); the ReXGlue SDK is BSD-3-Clause (portions derived from Xenia), and its licenses ship in the
release zip.
