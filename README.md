# OpenXcom on the New 3DS — scope, measurements, scaffold

> **Bring your own game files.** No game data is included: you need your own copy of *X-COM: UFO Defense* (GOG or Steam).

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
| `Dockerfile`, `build-3ds.sh` | one-shot build (game data is loaded from the SD card; `BAKE_DATA=1` builds it in) |
| `tools/measure_memory.py`, `tools/sdl_click_shim.c` | the headless measurement rig (desktop) |
| `patches/openxcom-3ds-reference.diff` | the resulting upstream diff, for reference |

## Build

```powershell
docker build -t openxcom-3ds .          # ~15 min: image + SDL_gfx + yaml-cpp for ARM
docker run --rm -v "${PWD}:/work" openxcom-3ds ./build-3ds.sh
```

`out/3ds/openxcom/openxcom.3dsx` → `sdmc:/3ds/openxcom/`, and copy the GEODATA
GEOGRAPH MAPS ROUTES SOUND TERRAIN UFOGRAPH UNITS folders from your own copy of the
game to `sdmc:/3ds/openxcom/UFO/`. Saves and `options.cfg` live in
`sdmc:/3ds/openxcom/user/`.

To build the data into the `.3dsx` instead, put the folders in `./xcom-data/` and run
with `-e BAKE_DATA=1`. **That file then contains the game itself: keep it on your own
3DS and never upload or share it.**

## Screen layout and controls (DS rule)

Same rule as the Freeciv port: **the game you touch is on the bottom screen,
the top screen is for glancing at.** Battles are the exception: the game goes
on the top screen and the touch screen becomes the battle pad. ZL swaps at any
time. The game composes one 320x400 SDL surface (rows 0-199 top, rows 200-399
touch screen), and the port puts it on the screens itself instead of SDL_Flip
(`openxcom3dsPresent` in `src/3ds/platform3ds.cpp`):

- **Game on the top screen: it fills the screen** (320x200 stretched to 400x240,
  nearest pixel, no side bars).
- **Sharp top screen:** with the 3D slider at 0 the top screen runs in its
  800x240 wide mode (`gfxSetWide`), so every real column is drawn; with the
  slider up it goes back to 400x240. The info panel stays 1:1 (2:1 across in
  wide mode) so its text keeps whole pixels.
- Touch screen: 320x200 at 1:1, centred.

**Game on the top screen, touch screen as a touchpad:** apart from the pad's own
buttons, sliding on the touch screen moves the game's cursor (relative, like a
laptop touchpad), a quick tap clicks where it points, R held + tap is a right
click (R's own job then happens on release, unless a tap used it). On the battle
pad, a quick tap on the close-up map still clicks that square.

**Without touch:** in the game's menus the D-pad moves a gold ring (and the
cursor) to the nearest button; A presses it (the button shows pressed while A is
held). The Circle Pad moves the cursor on whichever screen the game is on.

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
| D-pad | gold ring between buttons | rotate globe | scroll map |
| C-stick (New 3DS) | - | rotate globe | scroll map |
| L / R | - | zoom out / in | previous / next soldier |
| X | - | intercept | end turn (press twice) |
| Y | - | bases | kneel |
| ZR | - | - | view level up (tap) / down (hold) |
| ZL | swap screens | swap screens | swap screens |
| Touch, game on top | touchpad (slide, tap; R + tap = right click) | same | same; tap the close-up map to click a square |
| Start | instruction manual | instruction manual | instruction manual |
| Select | swap screens (same as ZL) | swap screens | swap screens |

Input notes: 3ds-sdl's own touch-to-mouse events are dropped (in dual-screen
mode it clamps the touch position to the top half). The touch screen is read
from the HID service directly, and `SDL_GetMouseState`/`SDL_WarpMouse` are
redirected to the port's cursor via a forced include (`3ds/mouse3ds.h`).

Desktop harness: the same patched tree builds natively with `-D__3DS__` and a
fake `3ds.h` (touch = lower half of the window) and runs under Xvfb; that is
how the layout and input are tested before hardware.

## In-game manual

Start opens a built-in manual (text on the top screen, section buttons on
the touch screen; Circle Pad / C-Stick scroll, D-pad moves between sections,
L/R change section, B or Start closes). The game waits while it is open. The text is `docs/manual.txt`,
compiled into `src/3ds/manual_text.h`; a `manual.txt` in
`sdmc:/3ds/openxcom/` replaces it without rebuilding.

## Phase 2 ideas (dual screen)

OpenXcom draws the Battlescape HUD into the same 320×200 surface, so a
true split (map on top, soldier panel + inventory on bottom) means teaching
`BattlescapeState` to blit its HUD surface to a second SDL surface. The 3DS
SDL port exposes both screens; `Screen::flip()` is the single place frames
go out, so that's where a second-surface blit would hook in. Stereoscopic
depth per map level (the Battlescape is already drawn level by level in
`Map::drawTerrain`) is the natural 3D feature, same layered technique as
the Micropolis and DCSS ports.

## Licence

GPL-3.0-or-later, the same as OpenXcom; the full text is in [`COPYING`](COPYING). The
build downloads OpenXcom and applies `tools/patch_openxcom.py`;
`patches/openxcom-3ds-reference.diff` shows every change made to it.

## Disclaimer

Not affiliated with or endorsed by the OpenXcom project, Firaxis, 2K, Take-Two,
MicroProse or Nintendo. *X-COM* and *UFO Defense* are trademarks of their owners and
are used here only to say which game files the engine reads. No game data is included.
