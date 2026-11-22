# Changelog

## 2026-11-21 (audit fixes)

- Top screen: every page the port draws there (battle dashboard, Geoscape dashboard, manual text, the Start menu's page, the controls card) now fills the whole 400x240 screen. They used to sit in a 320-wide column between black bars.
- Main menu: the top screen is a full controls card in large letters instead of a small box of fine print. The same card shows while you set up a New Battle (the empty "1st Jan 1999, $0" dashboard is gone).
- Battle dashboard: "Move: .. TU" no longer overlaps "KNEELING"; soldier names are shown whole (or as "F. Surname"), never with "..."; "Level" is no longer printed over the map squares; small labels are brighter.
- Battle pad: the face buttons are named under the soldier (kneel, end turn, view level); after the first X it says "X again: END TURN" and the End turn button reads Confirm.
- Touchpad card (game swapped to the top outside battle): the last line is no longer cut; helper text is brighter.
- Start menu: always on the touch screen, six full-width buttons; the top screen says what each does. "GAME OPTIONS" is now "SAVE / LOAD / OPTIONS" and opens with the pointer on Cancel, not on Abandon Game.
- EXIT GAME and HOME > Close save to their own file ("Exit save" in Load Game). The quick save is no longer overwritten by quitting.
- Main menu: Start, then CONTINUE loads the newest exit save or quick save.
- "Saving game" in a battle no longer makes the battle jump to the touch screen and back. While a game window is open on the touch screen, the dashboard says so.
- Updating the port no longer resets your Game Options: the port's settings are merged into options.cfg instead of replacing it.
- Manual: gold ring on the selected section, the house button look, text updated.

## 2026-11-21

- The touch screen's game is now stretched to the full 320x240 touch screen (no black bars at the top and bottom).
- Touch buttons give a much quieter click.
- Long names are cut with "..." at a letter boundary instead of mid-letter.
- QUICK LOAD in the Start menu asks "Load the quick save?" first, with NO selected.
- Smoother and faster: frames that did not change are skipped, and the D-pad focus ring redraws with the cursor.
- Closing the manual with B no longer also presses B in the game (on the Geoscape it opened the Options box).

## 2026-11-20

- Exit save: no longer attempted in the middle of the alien turn (it says NOT SAVED and why, and asks before quitting).
- HOME > Close saves like EXIT GAME; the game leaves memory for the next app.
- The log file stays open and is small; the full per-tap log needs an empty /3ds/openxcom/debug.txt.
- Install path: the program is /3ds/openxcom.3dsx.
- The main menu shows the build version.

## 2026-11-19

- Exit asks first when the quick save fails (SAVE FAILED screen).
- Manual: drag the stylus to scroll the text, with a little momentum.
