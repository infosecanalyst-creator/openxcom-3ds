# UX audit: OpenXcom on the New 3DS

Audited against stylus strategy games on DS/3DS (Advance Wars DS, Fire Emblem,
Civilization Revolution DS, Anno 1701 / Dawn of Discovery, Age of Empires DS),
touch ports of tactics games (XCOM: Enemy Within mobile, XCOM 2 on Switch,
Into the Breach) and the PC mouse conventions OpenXcom already follows.
Goal: the information the player needs, on screen, with the fewest taps.

## Layout rule
Bottom (touch) screen = the thing you act on. Top screen = what you glance at.
Nothing on the top screen needs touching.

## Findings and changes

| # | Problem (convention broken) | Change |
|---|---|---|
| 1 | Path preview and TU cost need **hover**; touch has no hover, so the first tap moved the soldier blind | `battleNewPreviewPath: 3` -- 1st tap previews path with TU numbers, 2nd tap moves (mobile XCOM). Move cost also shown on the top screen |
| 2 | **End turn on a single button press** (X) -- easy to trigger by accident | X must be pressed twice within 2.5 s; prompt on the top screen; any other button cancels |
| 3 | Comparing **fire modes** needs the action menu | Top screen lists Snap / Aimed / Auto with hit chance and TU cost; green = affordable now |
| 4 | Only one soldier's state visible | Squad roster (up to 12) with TU and health bars, downed and wounded marks, selected soldier highlighted |
| 5 | Threat awareness needs scanning the map | "N alien(s) in sight" on the top screen; aliens red on the minimap |
| 6 | Three lines of permanent control help | One context line; full controls on the menu screen and in the README |
| 7 | Avoidable confirmation screens | `skipNextTurnScreen`, `battleAutoEnd` (mission ends when the last alien falls), `battleInstantGrenade` |
| 8 | Geoscape: checking research / manufacturing means opening each base | Top screen shows every project with days remaining and production counts; "Nothing being researched" warns in orange |
| 9 | Craft readiness needs the Intercept window | Craft status and fuel per base on the top screen |
| 10 | Map scrolling competed with cursor and menu navigation | New 3DS C-stick scrolls map / globe; D-pad jumps between buttons in menus |
| 11 | Inventory stats, building queue, launch-anyway | `showMoreStatsInInventoryView`, `allowBuildingQueue`, `craftLaunchAlways` on |

## Not changed (deliberately)
- Gameplay rules (field promotions, psi changes, etc.) stay vanilla.
- The action menu itself (weapon slot -> mode -> target = 3 taps) is the
  engine's flow; the top-screen mode table makes the choice informed. A
  one-tap "fire best affordable mode at target" is a candidate for later.
- Touch targets are the original 320x200 art at 1:1. Buttons are ~16 px tall,
  below the ~26 px finger guideline but fine for a stylus (DS-era norm).
