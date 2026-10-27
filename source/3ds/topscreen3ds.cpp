// Top-screen information panel for the 3DS port.
//
// The bottom (touch) screen carries the game exactly as on PC; this panel
// shows what you would otherwise have to open a window for:
//   Battlescape: selected soldier (TU / energy / health / morale bars, key
//                stats, weapons), turn and level, and a live minimap.
//   Geoscape and base screens: date and time, funds, alien activity, and a
//                per-base summary (personnel, craft status).
//   Menus (no game loaded): title and the control scheme.
// Text uses the game's own fonts; colours are picked from whatever palette
// the game is currently using, so it always looks native.
#ifdef __3DS__
#include "topscreen3ds.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>
#include "../Engine/Game.h"
#include "../Engine/Screen.h"
#include "../Engine/Surface.h"
#include "../Engine/Language.h"
#include "../Engine/Unicode.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Mod/Unit.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Tile.h"
#include "../Savegame/GameTime.h"
#include "../Savegame/Base.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Ufo.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/Camera.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleResearch.h"
#include "../Savegame/Production.h"
#include "../Savegame/ResearchProject.h"
#include "../Battlescape/Pathfinding.h"

extern bool g_endTurnArmed3ds;   // platform3ds.cpp: X pressed once, waiting for confirm

namespace OpenXcom
{
namespace TopScreen3DS
{

static const int W = 320, H = 200;

// ---- colours: nearest entry in the live palette ------------------------
struct Rgb { int r, g, b; };
static Uint8 nearest(SDL_Color *pal, Rgb c)
{
	int best = 0, bestd = 1 << 30;
	for (int i = 1; i < 256; ++i)
	{
		const int dr = pal[i].r - c.r, dg = pal[i].g - c.g, db = pal[i].b - c.b;
		const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < bestd) { bestd = d; best = i; }
	}
	return (Uint8)best;
}

enum Col { C_TEXT, C_DIM, C_TITLE, C_GOOD, C_WARN, C_BAD, C_BLUE, C_FRAME, C_BG, C_PANEL, C_COUNT };
static const Rgb kRgb[C_COUNT] = {
	{ 230, 230, 230 }, { 140, 140, 150 }, { 250, 220, 90 }, { 90, 210, 90 },
	{ 230, 170, 60 }, { 220, 60, 50 }, { 90, 150, 240 }, { 70, 90, 140 },
	{ 0, 0, 0 }, { 16, 20, 34 },
};
// shade ramp per colour: glyph value 1 = full colour, higher = darker edge
static Uint8 g_col[C_COUNT];
static Uint8 g_ramp[C_COUNT][8];
static Uint32 g_palHash = 0;

static void buildColours(SDL_Color *pal)
{
	Uint32 h = 2166136261u;
	for (int i = 0; i < 256; ++i) { h = (h ^ pal[i].r) * 16777619u; h = (h ^ pal[i].g) * 16777619u; h = (h ^ pal[i].b) * 16777619u; }
	if (h == g_palHash) return;
	g_palHash = h;
	for (int c = 0; c < C_COUNT; ++c)
	{
		g_col[c] = nearest(pal, kRgb[c]);
		for (int s = 0; s < 8; ++s)
		{
			const int k = 100 - s * 14;
			Rgb d = { kRgb[c].r * k / 100, kRgb[c].g * k / 100, kRgb[c].b * k / 100 };
			g_ramp[c][s] = nearest(pal, d);
		}
	}
	// index 0 is "transparent/black" for surfaces; keep a true black entry
	g_col[C_BG] = 0;
}

// ---- text through the game's fonts ---------------------------------------
static Text *g_text = 0;          // scratch renderer, colour 0 -> raw glyph values
static Game *g_game = 0;

static int text(Surface *top, int x, int y, const std::string &s, Col c, bool big = false, int w = W)
{
	if (!g_text) return 0;
	if (big) g_text->setBig(); else g_text->setSmall();
	g_text->setX(0); g_text->setY(0);
	g_text->setColor(0);
	g_text->setText(s);
	g_text->clear();
	g_text->draw();
	SDL_Surface *src = g_text->getSurface();
	SDL_Surface *dst = top->getSurface();
	const int tw = std::min(w, std::min(src->w, W - x));
	const int th = std::min(src->h, H - y);
	int maxx = 0;
	for (int yy = 0; yy < th; ++yy)
	{
		const Uint8 *sp = (const Uint8 *)src->pixels + yy * src->pitch;
		Uint8 *dp = (Uint8 *)dst->pixels + (y + yy) * dst->pitch + x;
		for (int xx = 0; xx < tw; ++xx)
			if (sp[xx]) { dp[xx] = g_ramp[c][std::min(7, sp[xx] - 1)]; if (xx > maxx) maxx = xx; }
	}
	return x + maxx + 1;
}

static std::string str(const char *fmt, int a, int b = 0)
{
	char buf[64]; snprintf(buf, sizeof(buf), fmt, a, b); return buf;
}

static void rect(Surface *top, int x, int y, int w, int h, Col c) { top->drawRect(x, y, w, h, g_col[c]); }

static void frame(Surface *top, int x, int y, int w, int h)
{
	rect(top, x, y, w, h, C_FRAME);
	rect(top, x + 1, y + 1, w - 2, h - 2, C_PANEL);
}

static void bar(Surface *top, int x, int y, int w, const std::string &label, int v, int vmax, Col c)
{
	text(top, x, y, label, C_DIM);
	const int bx = x + 34, bw = w - 34 - 28;
	rect(top, bx, y + 1, bw, 6, C_FRAME);
	if (vmax > 0)
	{
		const int f = std::max(0, std::min(bw - 2, (bw - 2) * v / vmax));
		rect(top, bx + 1, y + 2, f, 4, c);
	}
	text(top, bx + bw + 3, y, str("%d", v), C_TEXT);
}

static std::string tr(const std::string &id) { return g_game->getLanguage()->getString(id); }

// ---- Battlescape ----------------------------------------------------------
static void drawMinimap(Surface *top, SavedBattleGame *battle, int x0, int y0, int w, int h)
{
	frame(top, x0, y0, w, h);
	const int mx = battle->getMapSizeX(), my = battle->getMapSizeY();
	if (mx <= 0 || my <= 0) return;
	int level = 0;
	if (BattlescapeState *bs = battle->getBattleState())
		if (bs->getMap() && bs->getMap()->getCamera()) level = bs->getMap()->getCamera()->getViewLevel();
	const int cell = std::max(1, std::min((w - 4) / mx, (h - 4) / my));
	const int ox = x0 + (w - cell * mx) / 2, oy = y0 + (h - cell * my) / 2;
	// like the game's minimap: every level from the ground up to the view
	// level, upper levels drawn over lower ones; undiscovered tiles hidden
	for (int lvl = 0; lvl <= level; ++lvl)
		for (int ty = 0; ty < my; ++ty)
			for (int tx = 0; tx < mx; ++tx)
			{
				Tile *t = battle->getTile(Position(tx, ty, lvl));
				if (!t || !t->isDiscovered(2)) continue;
				Col c = C_PANEL;
				if (t->getMapData(O_WESTWALL) || t->getMapData(O_NORTHWALL) || t->getMapData(O_OBJECT)) c = C_DIM;
				else if (t->getMapData(O_FLOOR)) c = C_FRAME;
				if (c != C_PANEL) rect(top, ox + tx * cell, oy + ty * cell, cell, cell, c);
			}
	BattleUnit *sel = battle->getSelectedUnit();
	const bool blink = (SDL_GetTicks() / 300) & 1;
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *u = *i;
		if (u->isOut()) continue;
		const Position p = u->getPosition();
		if (p.x < 0 || p.y < 0 || p.x >= mx || p.y >= my) continue;
		Col c;
		if (u->getFaction() == FACTION_PLAYER) c = (u == sel && blink) ? C_TEXT : C_GOOD;
		else if (u->getFaction() == FACTION_HOSTILE) { if (!u->getVisible()) continue; c = C_BAD; }
		else { if (!u->getVisible()) continue; c = C_TITLE; }
		const int s = std::max(2, cell);
		rect(top, ox + p.x * cell - (s - cell) / 2, oy + p.y * cell - (s - cell) / 2, s, s, c);
	}
	text(top, x0 + 4, y0 + h - 10, str("Level %d", level + 1), C_DIM);
}

static std::string itemLine(BattleItem *it)
{
	if (!it) return "-";
	std::string s = g_game->getLanguage()->getString(it->getRules()->getName());
	if (BattleItem *ammo = it->getAmmoItem())
		if (ammo != it) s += str(" (%d)", ammo->getAmmoQuantity());
	return s;
}

// Weapon modes for the selected soldier's main weapon: accuracy and TU cost
// for snap / aimed / auto at a glance, so choosing a shot needs no menu.
static void drawWeaponModes(Surface *top, BattleUnit *u, int x, int y)
{
	BattleItem *w = u->getItem("STR_RIGHT_HAND");
	if (!w || !w->getRules()) w = u->getItem("STR_LEFT_HAND");
	if (!w || !w->getRules())
	{
		text(top, x, y, "No weapon in hand", C_DIM);
		return;
	}
	RuleItem *r = w->getRules();
	text(top, x, y, itemLine(w), C_TEXT, false, 190);
	struct M { BattleActionType t; const char *n; int tu; };
	const M modes[3] = { { BA_SNAPSHOT, "Snap", r->getTUSnap() }, { BA_AIMEDSHOT, "Aimed", r->getTUAimed() },
	                     { BA_AUTOSHOT, "Auto", r->getTUAuto() } };
	int cx = x;
	for (int i = 0; i < 3; ++i)
	{
		if (modes[i].tu <= 0) continue;
		const int acc = u->getFiringAccuracy(modes[i].t, w);
		const int tu = u->getActionTUs(modes[i].t, w);
		const bool can = tu <= u->getTimeUnits();
		std::string s = std::string(modes[i].n) + str(" %d%%", acc) + str(" %dTU", tu);
		cx = text(top, cx, y + 10, s, can ? C_GOOD : C_DIM) + 8;
	}
}

// Every soldier at once: TU and health bars, out/wounded marks. The selected
// one is highlighted. Mirrors the squad strip of modern tactics games.
static void drawRoster(Surface *top, SavedBattleGame *battle, BattleUnit *sel, int x0, int y0, int w)
{
	int n = 0;
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
		if ((*i)->getFaction() == FACTION_PLAYER && (*i)->getOriginalFaction() == FACTION_PLAYER) ++n;
	if (!n) return;
	const int cols = 2, rows = (std::min(n, 12) + 1) / 2, cw = w / cols;
	int k = 0;
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end() && k < 12; ++i)
	{
		BattleUnit *u = *i;
		if (u->getFaction() != FACTION_PLAYER || u->getOriginalFaction() != FACTION_PLAYER) continue;
		const int x = x0 + (k / rows) * cw, y = y0 + (k % rows) * 9;
		++k;
		if (u == sel) rect(top, x, y, cw - 2, 9, C_FRAME);
		std::string nm = u->getName(g_game->getLanguage());
		if (nm.size() > 9) nm = nm.substr(0, 9);
		const bool out = u->isOut();
		text(top, x + 1, y, nm, out ? C_BAD : (u == sel ? C_TITLE : C_TEXT), false, 56);
		if (out) { text(top, x + 58, y, "DOWN", C_BAD); continue; }
		UnitStats *st = u->getBaseStats();
		const int bw = cw - 64;
		const int tu = st->tu ? (bw * u->getTimeUnits() / st->tu) : 0;
		const int hp = st->health ? (bw * u->getHealth() / st->health) : 0;
		rect(top, x + 58, y + 2, std::max(0, tu), 2, C_GOOD);
		rect(top, x + 58, y + 5, std::max(0, hp), 2, u->getFatalWounds() ? C_BAD : C_WARN);
	}
}

static int visibleEnemies(SavedBattleGame *battle)
{
	int n = 0;
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
		if ((*i)->getFaction() == FACTION_HOSTILE && !(*i)->isOut() && (*i)->getVisible()) ++n;
	return n;
}

static void drawBattle(Surface *top, SavedBattleGame *battle)
{
	const bool playerTurn = battle->getSide() == FACTION_PLAYER;
	text(top, 4, 3, str("TURN %d", battle->getTurn()), C_TITLE, true);
	text(top, 80, 7, playerTurn ? "X-COM" : (battle->getSide() == FACTION_HOSTILE ? "ALIENS" : "CIVILIANS"),
	     playerTurn ? C_GOOD : C_BAD);
	const int enemies = visibleEnemies(battle);
	text(top, 130, 7, enemies ? str("%d alien(s) in sight", enemies) : std::string("No aliens in sight"), enemies ? C_BAD : C_DIM);

	drawMinimap(top, battle, 222, 22, 96, 96);

	BattleUnit *u = battle->getSelectedUnit();
	frame(top, 2, 22, 218, 96);
	if (!u || u->getFaction() != FACTION_PLAYER)
	{
		text(top, 10, 30, playerTurn ? "No soldier selected (L/R to pick)" : "Enemy movement...", C_DIM);
	}
	else
	{
		UnitStats *st = u->getBaseStats();
		text(top, 6, 25, u->getName(g_game->getLanguage()), C_TEXT, true, 150);
		std::string rank = u->getRankString();
		if (!rank.empty()) text(top, 6, 41, tr(rank), C_DIM);
		if (u->isKneeled()) text(top, 160, 41, "KNEELING", C_WARN);
		// path preview cost (first tap previews, second tap moves)
		Pathfinding *pf = battle->getPathfinding();
		int barW = 212;
		bar(top, 6, 52, barW, "TU", u->getTimeUnits(), st->tu, C_GOOD);
		if (pf && pf->isPathPreviewed())
		{
			const int cost = pf->getTotalTUCost();
			const bool ok = cost <= u->getTimeUnits();
			text(top, 120, 41, str("Move: %d TU", cost), ok ? C_GOOD : C_BAD);
		}
		bar(top, 6, 61, barW, "Health", u->getHealth(), st->health, u->getFatalWounds() ? C_BAD : C_GOOD);
		bar(top, 6, 70, barW, "Energy", u->getEnergy(), st->stamina, C_WARN);
		bar(top, 6, 79, barW, "Morale", u->getMorale(), 100, C_BLUE);
		if (u->getFatalWounds()) text(top, 6, 88, str("FATAL WOUNDS: %d", u->getFatalWounds()), C_BAD);
		else text(top, 6, 88, str("React %d", st->reactions) + str("  Fire %d", st->firing) + str("  Brave %d", st->bravery), C_DIM);
		drawWeaponModes(top, u, 6, 98);
	}
	frame(top, 2, 120, 316, 62);
	drawRoster(top, battle, u, 5, 123, 310);
	if (g_endTurnArmed3ds)
		text(top, 4, 186, "Press X again to END TURN   (any other button cancels)", C_WARN);
	else
		text(top, 4, 186, "Tap: preview path, tap again: move   L/R soldier  X x2 end turn", C_DIM);
}

// ---- Geoscape / bases -----------------------------------------------------
static void drawStrategic(Surface *top, SavedGame *save)
{
	Language *lang = g_game->getLanguage();
	GameTime *t = save->getTime();
	if (t)
	{
		text(top, 4, 3, str("%02d:%02d", t->getHour(), t->getMinute()), C_TITLE, true);
		std::string date = t->getDayString(lang) + " " + std::string(lang->getString(t->getMonthString())) + str(" %d", t->getYear());
		text(top, 64, 7, date, C_TEXT);
	}
	text(top, 210, 7, Unicode::formatFunding(save->getFunds()), save->getFunds() < 0 ? C_BAD : C_GOOD);

	int ufos = 0;
	for (std::vector<Ufo*>::iterator i = save->getUfos()->begin(); i != save->getUfos()->end(); ++i)
		if ((*i)->getDetected()) ++ufos;
	frame(top, 2, 22, 316, 14);
	text(top, 8, 25, ufos ? str("UFOs on radar: %d", ufos) : std::string("No UFOs on radar"), ufos ? C_BAD : C_DIM);

	// Research and manufacturing: the usual reason to open a base screen.
	int y = 40;
	frame(top, 2, y, 316, 64);
	text(top, 8, y + 3, "RESEARCH", C_TITLE);
	text(top, 164, y + 3, "MANUFACTURE", C_TITLE);
	int ry = y + 14, my = y + 14;
	for (std::vector<Base*>::iterator b = save->getBases()->begin(); b != save->getBases()->end(); ++b)
	{
		const std::vector<ResearchProject*> &rp = (*b)->getResearch();
		for (std::vector<ResearchProject*>::const_iterator r = rp.begin(); r != rp.end() && ry < y + 60; ++r)
		{
			const int spent = (*r)->getSpent(), cost = (*r)->getCost(), sci = (*r)->getAssigned();
			std::string name = tr((*r)->getRules()->getName());
			text(top, 8, ry, name, C_TEXT, false, 108);
			if (sci > 0 && cost > spent) text(top, 118, ry, str("~%dd", (cost - spent + sci - 1) / sci), C_DIM);
			else text(top, 118, ry, sci ? "done" : "idle", sci ? C_GOOD : C_WARN);
			ry += 9;
		}
		const std::vector<Production*> &pp = (*b)->getProductions();
		for (std::vector<Production*>::const_iterator m = pp.begin(); m != pp.end() && my < y + 60; ++m)
		{
			std::string name = tr((*m)->getRules()->getName());
			text(top, 164, my, name, C_TEXT, false, 100);
			text(top, 268, my, str("%d/", (*m)->getAmountProduced()) + str("%d", (*m)->getAmountTotal()), C_DIM);
			my += 9;
		}
	}
	if (ry == y + 14) text(top, 8, ry, "Nothing being researched", C_WARN);
	if (my == y + 14) text(top, 164, my, "Nothing in production", C_DIM);

	// Craft: where they are and whether they can fly.
	y = 108;
	frame(top, 2, y, 316, 70);
	int cy = y + 3;
	for (std::vector<Base*>::iterator b = save->getBases()->begin(); b != save->getBases()->end() && cy < y + 62; ++b)
	{
		text(top, 8, cy, (*b)->getName(lang), C_TITLE, false, 120);
		text(top, 130, cy, str("Soldiers %d", (*b)->getAvailableSoldiers()) + str("  Sci %d", (*b)->getScientists()) + str("  Eng %d", (*b)->getEngineers()), C_DIM);
		cy += 10;
		for (std::vector<Craft*>::iterator c = (*b)->getCrafts()->begin(); c != (*b)->getCrafts()->end() && cy < y + 62; ++c)
		{
			Craft *cr = *c;
			text(top, 14, cy, cr->getName(lang), C_TEXT, false, 110);
			text(top, 130, cy, tr(cr->getStatus()), C_DIM, false, 100);
			text(top, 236, cy, str("Fuel %d%%", cr->getFuelPercentage()), cr->getFuelPercentage() < 30 ? C_WARN : C_DIM);
			cy += 9;
		}
	}
	text(top, 4, 186, "L/R zoom  X intercept  Y bases  Start manual", C_DIM);
}

// ---- menus ----------------------------------------------------------------
static void drawMenu(Surface *top)
{
	const std::string title = "X-COM: UFO DEFENSE";
	text(top, 84, 30, title, C_TITLE, true);
	text(top, 104, 50, "OpenXcom for New 3DS", C_DIM);
	frame(top, 40, 72, 240, 102);
	int y = 80;
	const char *lines[] = {
		"Tap the bottom screen: click",
		"Circle Pad cursor   A click   B back",
		"D-pad: jump between buttons",
		"C-stick: scroll map / globe",
		"Battle: tap to preview a path, tap",
		"  again to move.  L/R soldier  Y kneel",
		"  X twice: end turn   ZL/ZR level",
		"Start: instruction manual",
	};
	for (int i = 0; i < 8; ++i, y += 12) text(top, 52, y, lines[i], C_TEXT);
}

void render(Game *game, Surface *top)
{
	if (!game || !top) return;
	static Uint32 last = 0;
	const Uint32 now = SDL_GetTicks();
	if (now - last < 100 && last) return;    // 10 Hz is plenty for a status panel
	last = now;

	g_game = game;
	Mod *mod = game->getMod();
	if (!g_text && mod && game->getLanguage())
	{
		Font *big = mod->getFont("FONT_BIG", false), *small = mod->getFont("FONT_SMALL", false);
		if (!big || !small) return;
		g_text = new Text(W, 18, 0, 0);
		g_text->initText(big, small, game->getLanguage());
	}
	if (!g_text) return;

	SDL_Surface *s = game->getScreen()->getSurface()->getSurface();
	top->setPalette(s->format->palette->colors, 0, 256);
	g_text->setPalette(s->format->palette->colors, 0, 256);
	buildColours(s->format->palette->colors);

	top->clear(0);
	SavedGame *save = game->getSavedGame();
	if (save && save->getSavedBattle()) drawBattle(top, save->getSavedBattle());
	else if (save) drawStrategic(top, save);
	else drawMenu(top);
}

}
}
#endif
