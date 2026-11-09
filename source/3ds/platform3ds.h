#pragma once
// 3DS glue for OpenXcom: service setup (romfs, SD folders, New 3DS clock) and
// the input layer (circle pad = mouse cursor, A/B = clicks, touch mapped from
// the lower half of the dual-screen surface).
union SDL_Event;
void openxcom3dsInit();
void openxcom3dsPumpInput();                 // call once per frame before SDL_PollEvent
bool openxcom3dsTranslateEvent(SDL_Event &ev); // false = swallow this event
#include <string>
// Native 3DS software keyboard for text fields (base names, soldier names,
// save names). Returns false if the player cancelled.
bool openxcom3dsKeyboard(const std::string &current, std::string &out);
void openxcom3dsPostReturn();                // queue an Enter press for the game
// Layout: true while the game is shown on the top screen (battle), and the
// touch screen is the pad (topscreen3ds.cpp).
bool openxcom3dsGameOnTop();
void openxcom3dsToggleSwap();
bool openxcom3dsLefty();                     // left-handed mode: D-Pad and A/B/X/Y swapped
#include <SDL.h>
void openxcom3dsSendKey(SDLKey key);         // press + release, passed to the game as is
void openxcom3dsClickAt(int x, int y);       // left click at game coordinates
// Screen::flip, in place of SDL_Flip: puts the 320x400 frame (rows 0-199 top screen, 200-399 touch
// screen) on both screens. The top screen is drawn in its 800x240 wide mode while the 3D slider is
// at 0. gameOnTop: rows 0-199 are the game, stretched to fill the top screen; otherwise they are
// the info panel, 1:1. ring: draw the D-pad focus ring around the menu button under the cursor.
void openxcom3dsPresent(SDL_Surface *scr, bool gameOnTop, bool ring = true);
