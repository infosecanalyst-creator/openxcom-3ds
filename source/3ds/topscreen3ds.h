#pragma once
// Top-screen information panel for the 3DS port (DS layout rule: the game you
// touch is on the bottom screen; the top screen is for things you glance at).
namespace OpenXcom
{
class Game;
class Surface;
namespace TopScreen3DS
{
/// Redraws the 320x200 top panel for whatever the game is doing right now.
/// Cheap when nothing changed (re-renders at most every few frames).
void render(Game *game, Surface *top);
/// While the game is on the top screen this surface is the touch screen.
/// Stylus down at (x, y) in 320x200 space: presses the pad button there (with
/// its click and press animation). False if there is no button: the touch
/// screen is then a touchpad for the game's cursor (platform3ds.cpp).
bool padButton(int x, int y);
/// True if (x, y) is on the battle pad's close-up map.
bool onCloseMap(int x, int y);
/// A short tap on the close-up map: click that square in the battle.
bool mapTap(Game *game, int x, int y);
/// Redraw at the next frame (after a tap).
void invalidate();
}
}
