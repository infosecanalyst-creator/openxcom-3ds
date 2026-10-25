# OpenXcom on the New 3DS — scope, measurements, scaffold

**Verdict: feasible on the New 3DS, with real numbers behind it.**

## The measurement (the question this project hinged on)

OpenXcom master was built natively here and run headless (SDL dummy video,
clicks injected with an `LD_PRELOAD` shim) against your Steam copy of
*UFO Defense*, all the way into a tactical Battlescape:

| Stage | Peak RSS (x86-64) |
|---|---|
| Main menu, all resources loaded | 85 MB |
| New Battle → briefing | 89 MB |
| Pre-battle inventory → **Battlescape, turn 1** | **96 MB** |
| Main menu with **native-format audio** (11 kHz / 8-bit) | **67 MB** |

Of the 85 MB baseline, 68 MB is heap and **18 MB of that is just audio**:
SDL_mixer resamples the 2.7 MB of 11 kHz 8-bit originals to 22 kHz 16-bit
stereo. Keeping the native format removes it at no cost on a handheld
speaker. The rest is the yaml-cpp ruleset tree and decoded 8-bit surfaces,
both of which shrink on a 32-bit CPU (node/pointer-heavy data).

Estimate for the 3DS build: **~50–60 MB in a Battlescape**. The New 3DS
gives an app 124 MB. Comfortable. The Old 3DS's 64 MB is too tight to
promise.

Screens from the headless run are in `docs/`.

## Two findings that make the port simpler than expected

1. **OpenXcom master uses SDL 1.2, not SDL2** (`FindSDL2.cmake` is a
   misnomer; it looks for `libSDL`). devkitPro's `3ds-sdl` *is* SDL 1.2,
   with the bottom screen's touch mapped to the mouse. That is the whole
   display/input layer, for free.
2. **Only one file has platform code** (`Engine/CrossPlatform.cpp`): folder
   discovery, `getpwuid`, `backtrace`/`dladdr`, and `system()`-launched
   error dialogs. The patch here is 28 lines.

## What's in this folder

| Path | What |
|---|---|
| `tools/patch_openxcom.py` | the port edits (idempotent, anchored; stops if upstream moved an anchor) |
| `source/3ds/platform3ds.*` | romfs/SD setup, New 3DS clock, first-run `options.cfg` (400×240, native audio, no intro) |
| `source/Makefile.3ds` | cross build (based on upstream `Makefile.simple`; no CMake toolchain fight) |
| `tools/build-portlibs.sh` | cross-builds the two deps devkitPro doesn't ship: SDL_gfx and yaml-cpp |
| `Dockerfile`, `build-3ds.sh` | one-shot build; data in `./xcom-data/` is baked into romfs |
| `tools/measure_memory.py`, `tools/sdl_click_shim.c` | the headless measurement rig (desktop) |
| `patches/openxcom-3ds-reference.diff` | the resulting upstream diff, for reference |

## Build

```powershell
docker build -t openxcom-3ds .          # ~15 min: image + SDL_gfx + yaml-cpp for ARM
mkdir xcom-data; copy the GEODATA GEOGRAPH MAPS ROUTES SOUND TERRAIN UFOGRAPH UNITS folders in
docker run --rm -v "${PWD}:/work" openxcom-3ds ./build-3ds.sh
```

`out/3ds/openxcom/openxcom.3dsx` → `sdmc:/3ds/openxcom/`. Saves and
`options.cfg` live in `sdmc:/3ds/openxcom/user/`. If you'd rather not bake
the data in, put the X-COM folders in `sdmc:/3ds/openxcom/UFO/` instead.

## Screen layout and controls (DS rule)

Same rule as the Freeciv port: **the game you touch is on the bottom screen,
the top screen is for glancing at.** One 320x400 SDL surface is split by
3ds-sdl's dual-screen mode: rows 0-199 go to the top screen, rows 200-399 to
the bottom; both are 1:1 pixels (no stretching).

| Bottom screen (touch) | Top screen (info, `src/3ds/topscreen3ds.cpp`) |
|---|---|
| The game exactly as on PC | **Battle:** selected soldier (TU, energy, health, morale bars; reactions, firing, throwing, strength, bravery; wounds; both hands with ammo), turn and side, live minimap of the current level (your units, visible aliens and civilians) |
| | **Geoscape / bases:** time and date, funds, UFOs on radar, each base's soldiers / scientists / engineers and up to three craft with status and fuel |
| | **Menus:** title and the control scheme |

| Button | Menus | Geoscape | Battlescape |
|---|---|---|---|
| Touch | click | click | 1st tap previews path + TU cost, 2nd tap moves |
| Circle Pad | cursor | cursor | cursor |
| A | click | click | click |
| B | back (Esc) | back (Esc) | right click (turn) |
| D-pad | jump between buttons | rotate globe | scroll map |
| C-stick (New 3DS) | - | rotate globe | scroll map |
| L / R | - | zoom out / in | previous / next soldier |
| X | - | intercept | end turn (press twice) |
| Y | - | bases | kneel |
| ZL / ZR | - | - | view level down / up |
| Start / Select | OK (Enter) / cancel (Esc) | | |

Input notes: 3ds-sdl's own touch-to-mouse events are dropped (in dual-screen
mode it clamps the touch position to the top half). The touch screen is read
from the HID service directly, and `SDL_GetMouseState`/`SDL_WarpMouse` are
redirected to the port's cursor via a forced include (`3ds/mouse3ds.h`).

Desktop harness: the same patched tree builds natively with `-D__3DS__` and a
fake `3ds.h` (touch = lower half of the window) and runs under Xvfb; that is
how the layout and input are tested before hardware.

## Phase 2 ideas (dual screen)

OpenXcom draws the Battlescape HUD into the same 320×200 surface, so a
true split (map on top, soldier panel + inventory on bottom) means teaching
`BattlescapeState` to blit its HUD surface to a second SDL surface. The 3DS
SDL port exposes both screens; `Screen::flip()` is the single place frames
go out, so that's where a second-surface blit would hook in. Stereoscopic
depth per map level (the Battlescape is already drawn level by level in
`Map::drawTerrain`) is the natural 3D feature, same layered technique as
the Micropolis and DCSS ports.
