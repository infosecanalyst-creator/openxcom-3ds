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

static void drawBattle(Surface *top, SavedBattleGame *battle)
{
	const bool playerTurn = battle->getSide() == FACTION_PLAYER;
	text(top, 4, 3, str("TURN %d", battle->getTurn()), C_TITLE, true);
	text(top, 96, 7, playerTurn ? "X-COM" : (battle->getSide() == FACTION_HOSTILE ? "ALIENS" : "CIVILIANS"),
	     playerTurn ? C_GOOD : C_BAD);

	drawMinimap(top, battle, 206, 2, 112, 152);

	frame(top, 2, 22, 200, 132);
	BattleUnit *u = battle->getSelectedUnit();
	if (!u || u->getFaction() != FACTION_PLAYER)
	{
		text(top, 10, 30, playerTurn ? "No soldier selected" : "Enemy movement...", C_DIM);
	}
	else
	{
		UnitStats *st = u->getBaseStats();
		text(top, 8, 26, u->getName(g_game->getLanguage()), C_TEXT, true, 190);
		std::string rank = u->getRankString();
		if (!rank.empty()) text(top, 8, 43, tr(rank), C_DIM);
		if (u->isKneeled()) text(top, 150, 43, "KNEELING", C_WARN);
		bar(top, 8, 56, 190, "TU", u->getTimeUnits(), st->tu, C_GOOD);
		bar(top, 8, 66, 190, "Energy", u->getEnergy(), st->stamina, C_WARN);
		bar(top, 8, 76, 190, "Health", u->getHealth(), st->health, u->getFatalWounds() ? C_BAD : C_GOOD);
		bar(top, 8, 86, 190, "Morale", u->getMorale(), 100, C_BLUE);
		text(top, 8, 98, str("React %d", st->reactions), C_TEXT);
		text(top, 70, 98, str("Fire %d", st->firing), C_TEXT);
		text(top, 128, 98, str("Throw %d", st->throwing), C_TEXT);
		text(top, 8, 108, str("Str %d", st->strength), C_TEXT);
		text(top, 70, 108, str("Brave %d", st->bravery), C_TEXT);
		if (u->getFatalWounds()) text(top, 128, 108, str("Wounds %d", u->getFatalWounds()), C_BAD);
		text(top, 8, 122, "R:", C_DIM); text(top, 22, 122, itemLine(u->getItem("STR_RIGHT_HAND")), C_TEXT, false, 176);
		text(top, 8, 132, "L:", C_DIM); text(top, 22, 132, itemLine(u->getItem("STR_LEFT_HAND")), C_TEXT, false, 176);
	}
	text(top, 4, 160, "L/R soldier   Y kneel   X end turn", C_DIM);
	text(top, 4, 170, "D-pad scroll   ZL/ZR level   Select menu", C_DIM);
	text(top, 4, 182, "Circle Pad cursor   A click   B right-click", C_DIM);
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
	frame(top, 2, 22, 316, 16);
	text(top, 8, 26, ufos ? str("UFOs on radar: %d", ufos) : std::string("No UFOs on radar"), ufos ? C_BAD : C_DIM);

	int y = 42;
	for (std::vector<Base*>::iterator b = save->getBases()->begin(); b != save->getBases()->end() && y < 156; ++b)
	{
		Base *base = *b;
		const int rows = 1 + std::min<int>(3, base->getCrafts()->size());
		const int h = 12 + rows * 10;
		if (y + h > 158) break;
		frame(top, 2, y, 316, h);
		text(top, 8, y + 3, base->getName(lang), C_TITLE);
		text(top, 130, y + 3, str("Soldiers %d", base->getAvailableSoldiers()), C_TEXT);
		text(top, 200, y + 3, str("Sci %d", base->getScientists()), C_TEXT);
		text(top, 252, y + 3, str("Eng %d", base->getEngineers()), C_TEXT);
		int cy = y + 14, n = 0;
		for (std::vector<Craft*>::iterator c = base->getCrafts()->begin(); c != base->getCrafts()->end() && n < 3; ++c, ++n)
		{
			Craft *cr = *c;
			text(top, 14, cy, cr->getName(lang), C_TEXT, false, 110);
			text(top, 130, cy, tr(cr->getStatus()), C_DIM, false, 100);
			text(top, 236, cy, str("Fuel %d%%", cr->getFuelPercentage()), cr->getFuelPercentage() < 30 ? C_WARN : C_DIM);
			cy += 10;
		}
		y += h + 2;
	}
	text(top, 4, 170, "L/R zoom   D-pad rotate   Select options", C_DIM);
	text(top, 4, 182, "Circle Pad cursor   A click   B right-click", C_DIM);
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
		"Tap the bottom screen to click",
		"Circle Pad  move cursor",
		"A  click     B  right-click / back",
		"D-pad  move between buttons",
		"Start  OK    Select  cancel / menu",
		"In battle:  L/R soldier  Y kneel",
		"            X end turn",
	};
	for (int i = 0; i < 7; ++i, y += 12) text(top, 52, y, lines[i], C_TEXT);
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
