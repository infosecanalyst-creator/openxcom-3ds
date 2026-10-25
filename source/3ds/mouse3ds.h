#pragma once
// Force-included into every OpenXcom file on the 3DS (-include 3ds/mouse3ds.h).
// The game reads the mouse through SDL_GetMouseState/SDL_WarpMouse; on the 3DS
// those are redirected to the port's own cursor (touch screen read directly
// from the hardware, Circle Pad, A/B), because 3ds-sdl clamps the dual-screen
// touch position to the top half.
#ifdef __3DS__
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
Uint8 openxcom3dsGetMouseState(int *x, int *y);
void openxcom3dsWarpMouse(Uint16 x, Uint16 y);
#ifdef __cplusplus
}
#endif
// function-like so "(SDL_GetMouseState)(..)" still reaches SDL itself
#define SDL_GetMouseState(x, y) openxcom3dsGetMouseState(x, y)
#define SDL_WarpMouse(x, y) openxcom3dsWarpMouse(x, y)
#endif
