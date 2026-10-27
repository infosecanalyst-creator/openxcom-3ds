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
