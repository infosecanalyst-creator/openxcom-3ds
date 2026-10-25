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
}
}
