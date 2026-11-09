#ifdef __3DS__
#include <3ds.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include "platform3ds.h"
#include <SDL.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Options.h"
#include "../Interface/TextButton.h"
#include "../Interface/ImageButton.h"
#include "../Interface/BattlescapeButton.h"
#include "../Interface/ComboBox.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Geoscape/GeoscapeState.h"
#include "topscreen3ds.h"
#include "../Engine/Logger.h"
#include <typeinfo>

// yaml-cpp's ruleset parsing is deeply recursive; give the main thread a
// 4 MB stack instead of libctru's 32 KB default.
extern "C" u32 __stacksize__ = 4 * 1024 * 1024;

// newlib on the 3DS lacks these; OpenXcom only needs harmless versions.
extern "C" {
mode_t umask(mode_t) { return 0; }
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *p = memalign(alignment, size);
	if (!p) return ENOMEM;
	*memptr = p;
	return 0;
}
FILE *popen(const char *, const char *) { return NULL; }
int pclose(FILE *) { return -1; }
}

static void shutdown3ds()
{
	romfsExit();
}

void openxcom3dsInit()
{
	osSetSpeedupEnable(true);        // New 3DS: 804 MHz + L2 cache
	romfsInit();
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/openxcom", 0777);
	mkdir("sdmc:/3ds/openxcom/user", 0777);
	mkdir("sdmc:/3ds/openxcom/user/mods", 0777);
	// No atexit(romfsExit): newlib closes still-open FILE*s after atexit
	// handlers run, and closing a romfs file after unmount derefs a NULL device.
	(void)shutdown3ds;
	// Default options, written on first run and again whenever the port's
	// config version changes (OpenXcom rewrites options.cfg itself, so the
	// version lives in a side file). Native audio format saves ~18 MB; the
	// game runs on the bottom (touch) screen at its native 320x200. 16-bit
	// audio is required for the AdLib soundtrack (the 3DS can't play MIDI).
	static const char *kCfgVersion = "5\n";
	static const char *kVerPath = "sdmc:/3ds/openxcom/user/port-config-version";
	bool writeCfg = true;
	if (FILE *f = fopen(kVerPath, "r"))
	{
		char line[16] = {0};
		if (fgets(line, sizeof(line), f) && strcmp(line, kCfgVersion) == 0) writeCfg = false;
		fclose(f);
	}
	if (writeCfg)
	{
		if (FILE *f = fopen("sdmc:/3ds/openxcom/user/options.cfg", "w"))
		{
			fputs("options:\n"
			      "  displayWidth: 320\n  displayHeight: 200\n  fullscreen: false\n"
			      "  audioBitDepth: 16\n  audioSampleRate: 11025\n  audioChunkSize: 1024\n"
			      "  playIntro: false\n  useOpenGL: false\n  keepAspectRatio: false\n"
			      "  dragScrollButton: 1\n"
			      // touch/QoL defaults (UX audit): preview a path with the first tap and
			      // move with the second; fewer confirmation screens; more info shown
			      "  battleNewPreviewPath: 3\n  skipNextTurnScreen: true\n  battleAutoEnd: true\n"
			      "  battleInstantGrenade: true\n  battleTooltips: true\n  showMoreStatsInInventoryView: true\n"
			      "  allowBuildingQueue: true\n  craftLaunchAlways: true\n  showFundsOnGeoscape: true\n"
			      "  globeFlightPaths: true\n  autosave: true\n", f);
			fclose(f);
		}
		if (FILE *f = fopen(kVerPath, "w")) { fputs(kCfgVersion, f); fclose(f); }
	}
}

// ---- input ------------------------------------------------------------------
// Surface layout (see Screen.cpp): rows 0-199 = top screen (info panel),
// rows 200-399 = bottom (touch) screen showing the game. The game only ever
// sees game-space coordinates (0..319, 0..199).
//
// 3ds-sdl's own touch-to-mouse events are unreliable in dual-screen mode (the
// position is clamped to the top half), so they are dropped. Touch is read
// straight from the HID service; Circle Pad and A/B drive the same cursor;
// every mouse event the game sees is posted here and tagged with kMark.
// SDL_GetMouseState/SDL_WarpMouse are redirected here (3ds/mouse3ds.h).
bool g_endTurnArmed3ds = false;   // shown on the top screen (topscreen3ds.cpp)

namespace
{
const int kGameW = 320, kGameH = 200;
Uint32 g_endTurnArmedAt = 0;
bool g_endTurnSentDown = false;
u32 g_cstickPrev = 0;
SDL_Joystick *g_pad = NULL;
bool g_padTried = false;
float g_cx = kGameW / 2, g_cy = kGameH / 2;   // cursor, game space
Uint32 g_lastTick = 0;
Uint8 g_buttons = 0;                          // SDL_BUTTON masks currently held
bool g_touching = false;
// Game on the top screen: the touch screen is the pad. A stylus-down on a pad button presses it
// (PT_IGNORE until the stylus lifts); anywhere else the touch screen is a touchpad (PT_PAD): a
// slide moves the game's cursor by how far it slides (like a laptop's), a short tap clicks where
// the cursor points (R held: right click), and a short tap on the battle pad's close-up map
// clicks that square.
enum PadTouch { PT_NONE, PT_IGNORE, PT_PAD };
PadTouch g_pt = PT_NONE;
int g_ptLastX = 0, g_ptLastY = 0, g_ptStartX = 0, g_ptStartY = 0, g_ptMoved = 0;
Uint32 g_ptStart = 0;
bool g_ptOnMap = false;
Uint8 g_tapButton = 0;                        // touchpad tap: button held until g_tapReleaseAt
Uint32 g_tapReleaseAt = 0;
bool g_rHeld = false, g_rUsed = false;        // R while the game is on top (right-click modifier)
bool g_userFlip = false;                      // SELECT / Swap: flip the automatic layout
int g_lastAutoTop = -1;
const Uint8 kMark = 0x3D;                     // "which" tag on events we post
const Uint8 kPad = 0x3E;                      // tag on pad-button keys we generate from the HID state
// touch -> game space: each 320x200 half is drawn 1:1, centred (y 20..219)
inline int touchToGameY(int py) { return py - 20; }

void postMotion(int dx, int dy)
{
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = SDL_MOUSEMOTION; e.motion.which = kMark; e.motion.state = g_buttons;
	e.motion.x = (Uint16)g_cx; e.motion.y = (Uint16)g_cy; e.motion.xrel = dx; e.motion.yrel = dy;
	SDL_PushEvent(&e);
}

void postButton(Uint8 button, bool down)
{
	if (down) g_buttons |= SDL_BUTTON(button); else g_buttons &= ~SDL_BUTTON(button);
	if (down)
	{
		OpenXcom::Game *g = OpenXcom::State::game3ds();
		OpenXcom::State *st = g ? g->getTopState3ds() : 0;
		OpenXcom::Logger().get(OpenXcom::LOG_INFO) << "3ds click " << (int)button << " at " << (int)g_cx << "," << (int)g_cy
		              << " state " << (st ? typeid(*st).name() : "-");
	}
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button.which = kMark; e.button.button = button; e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.x = (Uint16)g_cx; e.button.y = (Uint16)g_cy;
	SDL_PushEvent(&e);
}

enum Ctx { CTX_MENU, CTX_GEOSCAPE, CTX_BATTLE };

Ctx context(OpenXcom::State **out)
{
	OpenXcom::Game *game = OpenXcom::State::game3ds();
	OpenXcom::State *st = game ? game->getTopState3ds() : 0;
	if (out) *out = st;
	if (!st) return CTX_MENU;
	if (dynamic_cast<OpenXcom::BattlescapeState*>(st)) return CTX_BATTLE;
	if (dynamic_cast<OpenXcom::GeoscapeState*>(st)) return CTX_GEOSCAPE;
	return CTX_MENU;
}

void moveCursor(float x, float y)
{
	const float ox = g_cx, oy = g_cy;
	g_cx = std::max(0.f, std::min((float)kGameW - 1, x));
	g_cy = std::max(0.f, std::min((float)kGameH - 1, y));
	if ((int)ox != (int)g_cx || (int)oy != (int)g_cy) postMotion((int)g_cx - (int)ox, (int)g_cy - (int)oy);
}

void touchpadTap()
{
	if (g_ptOnMap && OpenXcom::TopScreen3DS::mapTap(OpenXcom::State::game3ds(), g_ptStartX, touchToGameY(g_ptStartY)))
		return;
	const bool right = (hidKeysHeld() & KEY_R) != 0;
	if (right) g_rUsed = true;
	if (g_tapButton) postButton(g_tapButton, false);
	g_tapButton = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
	postButton(g_tapButton, true);
	g_tapReleaseAt = SDL_GetTicks() + 100;
}

bool clickable(OpenXcom::Surface *s)
{
	if (!s || !s->getVisible()) return false;
	return dynamic_cast<OpenXcom::TextButton*>(s) || dynamic_cast<OpenXcom::ImageButton*>(s)
	    || dynamic_cast<OpenXcom::BattlescapeButton*>(s) || dynamic_cast<OpenXcom::ComboBox*>(s);
}

// D-pad in menus: jump the cursor to the nearest button in that direction.
void snapFocus(OpenXcom::State *st, int dx, int dy)
{
	if (!st) return;
	const std::vector<OpenXcom::Surface*> &v = st->getSurfaces3ds();
	float best = 1e9f, bx = g_cx, by = g_cy;
	bool onButton = false;
	for (size_t i = 0; i < v.size(); ++i)
	{
		OpenXcom::Surface *s = v[i];
		if (!clickable(s)) continue;
		if (g_cx >= s->getX() && g_cx < s->getX() + s->getWidth() && g_cy >= s->getY() && g_cy < s->getY() + s->getHeight())
			onButton = true;
	}
	for (size_t i = 0; i < v.size(); ++i)
	{
		OpenXcom::Surface *s = v[i];
		if (!clickable(s)) continue;
		const float cx = s->getX() + s->getWidth() * 0.5f, cy = s->getY() + s->getHeight() * 0.5f;
		const float ex = cx - g_cx, ey = cy - g_cy;
		float along = ex * dx + ey * dy, across = fabsf(ex * dy) + fabsf(ey * dx);
		if (onButton && along < 4.f) continue;            // must actually move that way
		if (!onButton) along = fabsf(along);             // first press: nearest button
		const float score = along + across * 2.f;
		if (score < best) { best = score; bx = cx; by = cy; }
	}
	if (best < 1e9f) moveCursor(bx, by);
}

void remap(SDL_Event &ev, Ctx ctx)
{
	SDLKey &k = ev.key.keysym.sym;
	if (ctx == CTX_BATTLE)
	{
		if (k == SDLK_l) k = OpenXcom::Options::keyBattlePrevUnit;
		else if (k == SDLK_r) k = OpenXcom::Options::keyBattleNextUnit;
		else if (k == SDLK_y) k = OpenXcom::Options::keyBattleKneel;
	}
	else if (ctx == CTX_GEOSCAPE)
	{
		if (k == SDLK_l) k = OpenXcom::Options::keyGeoZoomOut;
		else if (k == SDLK_r) k = OpenXcom::Options::keyGeoZoomIn;
		else if (k == SDLK_x) k = OpenXcom::Options::keyGeoIntercept;
		else if (k == SDLK_y) k = OpenXcom::Options::keyGeoBases;
	}
}
}

// ---- in-game manual (Start) -------------------------------------------------
// Runs its own little loop while open, so the game (and Geoscape time) waits
// underneath. Text on the top screen, section buttons on the touch screen.
#include "manual3ds.h"
#include "click3ds.h"
#include "manual_text.h"
namespace
{
manual3ds::Manual g_manual;
bool g_manualLoaded = false;
bool g_touchBlocked = false;    // stylus still down after closing the manual

void blitCanvas(SDL_Surface *scr, const manual3ds::Canvas &cv, int y0)
{
	if (SDL_MUSTLOCK(scr)) SDL_LockSurface(scr);
	const int bpp = scr->format->BytesPerPixel;
	for (int y = 0; y < cv.h && y0 + y < scr->h; ++y)
	{
		Uint8 *row = (Uint8 *)scr->pixels + (y0 + y) * scr->pitch;
		const uint8_t *src = cv.px + y * cv.w * 3;
		for (int x = 0; x < cv.w && x < scr->w; ++x, src += 3)
		{
			const Uint32 c = SDL_MapRGB(scr->format, src[0], src[1], src[2]);
			if (bpp == 4) ((Uint32 *)row)[x] = c;
			else if (bpp == 2) ((Uint16 *)row)[x] = (Uint16)c;
		}
	}
	if (SDL_MUSTLOCK(scr)) SDL_UnlockSurface(scr);
}

// Left-handed play: the D-Pad and the A/B/X/Y face buttons swap roles. Persisted by a marker file.
static const char *kLeftyPath = "sdmc:/3ds/openxcom/.lefty";
static bool g_lefty = false, g_leftyLoaded = false;
static void loadLefty()
{
	struct stat sb;
	g_lefty = stat(kLeftyPath, &sb) == 0;
	g_leftyLoaded = true;
}
static void setLefty(bool on)
{
	g_lefty = on;
	if (on) { if (FILE *f = fopen(kLeftyPath, "w")) fclose(f); }
	else remove(kLeftyPath);
}
static u32 remapKeys(u32 m)
{
	if (!g_lefty) return m;
	static const struct { u32 a, b; } pairs[] = {
		{ KEY_Y, KEY_DUP }, { KEY_A, KEY_DRIGHT }, { KEY_B, KEY_DDOWN }, { KEY_X, KEY_DLEFT } };
	u32 r = m;
	for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
		r &= ~(pairs[i].a | pairs[i].b);
	for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
	{
		if (m & pairs[i].a) r |= pairs[i].b;
		if (m & pairs[i].b) r |= pairs[i].a;
	}
	return r;
}

void runManual()
{
	if (!g_leftyLoaded) loadLefty();
	if (!g_manualLoaded)
	{
		g_manual.load(kManualText, "sdmc:/3ds/openxcom/manual.txt", "OpenXcom for 3DS: manual");
		g_manualLoaded = true;
	}
	SDL_Surface *scr = SDL_GetVideoSurface();
	if (!scr) return;
	// release any held mouse button so the game doesn't see a stuck click
	if (g_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) postButton(SDL_BUTTON_LEFT, false);
	if (g_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) postButton(SDL_BUTTON_RIGHT, false);
	g_touching = false;
	if (g_tapButton) { postButton(g_tapButton, false); g_tapButton = 0; }
	g_pt = PT_NONE;

	const int half = scr->h / 2;               // 200: top rows, then the touch screen
	static std::vector<uint8_t> topPx, botPx;
	topPx.assign(scr->w * half * 3, 0);
	botPx.assign(scr->w * half * 3, 0);
	manual3ds::Canvas top = { scr->w, half, topPx.data() };
	manual3ds::Canvas bot = { scr->w, half, botPx.data() };

	g_manual.open();
	hidSetRepeatParameters(20, 6);
	bool redraw = true;
	while (aptMainLoop())
	{
		hidScanInput();
		const u32 down = remapKeys(hidKeysDown()), rep = remapKeys(hidKeysDownRepeat());
		manual3ds::Input mi;
		g_manual.set_toggle(g_lefty ? "Left hand: on" : "Left hand: off");
		static const struct { u32 b; uint32_t k; bool rep; } map[] = {
			{ KEY_DUP, manual3ds::K_UP, true }, { KEY_DDOWN, manual3ds::K_DOWN, true },
			{ KEY_CSTICK_UP, manual3ds::K_SCROLL_UP, true }, { KEY_CSTICK_DOWN, manual3ds::K_SCROLL_DOWN, true },
			{ KEY_DLEFT, manual3ds::K_LEFT, true }, { KEY_DRIGHT, manual3ds::K_RIGHT, true },
			{ KEY_L, manual3ds::K_L, false }, { KEY_R, manual3ds::K_R, false },
			{ KEY_A, manual3ds::K_A, false }, { KEY_B, manual3ds::K_B, false },
			{ KEY_START, manual3ds::K_START, false },
		};
		for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
			if ((map[i].rep ? rep : down) & map[i].b) mi.down |= map[i].k;
		if (down & KEY_TOUCH)
		{
			touchPosition tp;
			hidTouchRead(&tp);
			mi.touch_down = true;
			mi.tx = tp.px;
			mi.ty = touchToGameY(tp.py);       // the 320x200 bottom half is centred
		}
		circlePosition cp;
		hidCircleRead(&cp);
		mi.cpad_y = cp.dy;
		const bool wasDirty = g_manual.dirty();
		if (!g_manual.update(mi)) break;
		if (g_manual.take_toggle()) { setLefty(!g_lefty); redraw = true; }
		if (g_manual.take_click()) click3ds::play();   // standard: every tap clicks
		if (redraw || mi.down || mi.touch_down || wasDirty || g_manual.dirty())
		{
			g_manual.draw_top(top);
			g_manual.draw_bottom(bot);
			blitCanvas(scr, top, 0);
			blitCanvas(scr, bot, half);
			openxcom3dsPresent(scr, false, false);
			redraw = false;
		}
		else
			gspWaitForVBlank();
	}
	// don't let the buttons that closed the manual reach the game
	g_touchBlocked = (hidKeysHeld() & KEY_TOUCH) != 0;
	SDL_PumpEvents();
	SDL_Event ev;
	while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENTMASK(SDL_KEYDOWN) | SDL_EVENTMASK(SDL_KEYUP)
	                      | SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) | SDL_EVENTMASK(SDL_MOUSEBUTTONUP)) > 0)
		;
	g_lastTick = 0;                            // no cursor jump from the long frame
}
}

void openxcom3dsPumpInput()
{
	if (!g_padTried)
	{
		g_padTried = true;
		if (!SDL_WasInit(SDL_INIT_JOYSTICK)) SDL_InitSubSystem(SDL_INIT_JOYSTICK);
		if (SDL_NumJoysticks() > 0) g_pad = SDL_JoystickOpen(0);
		SDL_JoystickEventState(SDL_IGNORE);
	}
	// a touchpad tap holds its button 100 ms, so the game's buttons show pressed
	if (g_tapButton && SDL_GetTicks() >= g_tapReleaseAt) { postButton(g_tapButton, false); g_tapButton = 0; }
	// touch screen, straight from the hardware
	if (g_touchBlocked)
	{
		if (!(hidKeysHeld() & KEY_TOUCH)) g_touchBlocked = false;
	}
	else if (g_pt != PT_NONE)
	{
		if (!(hidKeysHeld() & KEY_TOUCH))
		{
			if (g_pt == PT_PAD && g_ptMoved < 8 && SDL_GetTicks() - g_ptStart < 350) touchpadTap();
			g_pt = PT_NONE;
		}
		else if (g_pt == PT_PAD)
		{
			touchPosition tp;
			hidTouchRead(&tp);
			const int dx = tp.px - g_ptLastX, dy = tp.py - g_ptLastY;
			if (dx || dy)
			{
				// a little faster than 1:1, so one slide crosses the game's 320x200
				moveCursor(g_cx + dx * 1.25f, g_cy + dy * 1.1f);
				g_ptMoved += abs(dx) + abs(dy);
				g_ptLastX = tp.px; g_ptLastY = tp.py;
			}
		}
	}
	else if (!g_touching && (hidKeysDown() & KEY_TOUCH) && openxcom3dsGameOnTop())
	{
		touchPosition tp;
		hidTouchRead(&tp);
		const int gy = touchToGameY(tp.py);
		if (OpenXcom::TopScreen3DS::padButton(tp.px, gy)) g_pt = PT_IGNORE;
		else
		{
			g_pt = PT_PAD;
			g_ptLastX = g_ptStartX = tp.px; g_ptLastY = g_ptStartY = tp.py;
			g_ptMoved = 0; g_ptStart = SDL_GetTicks();
			g_ptOnMap = OpenXcom::TopScreen3DS::onCloseMap(tp.px, gy);
			if (g_rHeld) g_rUsed = true;             // R was a modifier this time, not "next soldier"
		}
	}
	else if (hidKeysHeld() & KEY_TOUCH)
	{
		touchPosition tp;
		hidTouchRead(&tp);
		if (tp.px || tp.py)
		{
			moveCursor((float)tp.px, (float)touchToGameY(tp.py));
			if (!g_touching) { g_touching = true; postButton(SDL_BUTTON_LEFT, true); }
		}
	}
	else if (g_touching)
	{
		g_touching = false;
		postButton(SDL_BUTTON_LEFT, false);
	}

	// New 3DS C-stick scrolls the map / globe (arrow keys), leaving the D-pad
	// and Circle Pad free.
	{
		const u32 held = hidKeysHeld() & (KEY_CSTICK_UP | KEY_CSTICK_DOWN | KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT);
		const u32 changed = held ^ g_cstickPrev;
		static const struct { u32 bit; SDLKey key; } map[4] = {
			{ KEY_CSTICK_UP, SDLK_UP }, { KEY_CSTICK_DOWN, SDLK_DOWN }, { KEY_CSTICK_LEFT, SDLK_LEFT }, { KEY_CSTICK_RIGHT, SDLK_RIGHT } };
		for (int i = 0; i < 4; ++i)
			if (changed & map[i].bit)
			{
				SDL_Event e; memset(&e, 0, sizeof(e));
				e.type = (held & map[i].bit) ? SDL_KEYDOWN : SDL_KEYUP;
				e.key.state = (held & map[i].bit) ? SDL_PRESSED : SDL_RELEASED;
				e.key.which = kMark;                       // so menus don't treat it as D-pad focus
				e.key.keysym.sym = map[i].key;
				SDL_PushEvent(&e);
			}
		g_cstickPrev = held;
	}
	// Pad buttons: read from the HID state here instead of relying on 3ds-sdl's own key
	// events (in Azahar none of them arrived, so ZL/START/A/B did nothing). Tagged kPad;
	// translateEvent treats them as ordinary button keys and drops the library's copies.
	{
		static const struct { u32 bit; SDLKey key; } pm[] = {
			{ KEY_A, SDLK_a }, { KEY_B, SDLK_b }, { KEY_X, SDLK_x }, { KEY_Y, SDLK_y },
			{ KEY_L, SDLK_l }, { KEY_R, SDLK_r }, { KEY_START, SDLK_RETURN }, { KEY_SELECT, SDLK_ESCAPE },
			{ KEY_ZL, SDLK_LSHIFT }, { KEY_ZR, SDLK_RSHIFT },
			{ KEY_DUP, SDLK_UP }, { KEY_DDOWN, SDLK_DOWN }, { KEY_DLEFT, SDLK_LEFT }, { KEY_DRIGHT, SDLK_RIGHT } };
		if (!g_leftyLoaded) loadLefty();
		const u32 kd = remapKeys(hidKeysDown()), ku = remapKeys(hidKeysUp());
		for (size_t i = 0; i < sizeof(pm) / sizeof(pm[0]); ++i)
			for (int up = 0; up < 2; ++up)
				if ((up ? ku : kd) & pm[i].bit)
				{
					SDL_Event e; memset(&e, 0, sizeof(e));
					e.type = up ? SDL_KEYUP : SDL_KEYDOWN;
					e.key.state = up ? SDL_RELEASED : SDL_PRESSED;
					e.key.which = kPad;
					e.key.keysym.sym = pm[i].key;
					SDL_PushEvent(&e);
				}
	}
	if (g_endTurnArmed3ds && SDL_GetTicks() - g_endTurnArmedAt > 2500) g_endTurnArmed3ds = false;

	const Uint32 now = SDL_GetTicks();
	const float dt = g_lastTick ? (now - g_lastTick) / 1000.f : 0.f;
	g_lastTick = now;
	if (!g_pad || dt <= 0.f || dt > 0.25f || g_touching) return;
	SDL_JoystickUpdate();
	const int ax = SDL_JoystickGetAxis(g_pad, 0), ay = SDL_JoystickGetAxis(g_pad, 1);
	// works whether the port scales the pad to +-32767 or reports raw +-156
	const int full = std::max(std::max(abs(ax), abs(ay)), 1) > 400 ? 32767 : 156;
	float fx = ax / (float)full, fy = ay / (float)full;
	if (fabsf(fx) < 0.15f && fabsf(fy) < 0.15f) return;
	fx *= fabsf(fx); fy *= fabsf(fy);                   // fine near centre, fast at the rim
	const float speed = 260.f;                          // px/s at full deflection
	moveCursor(g_cx + fx * speed * dt, g_cy + fy * speed * dt);
}

bool openxcom3dsTranslateEvent(SDL_Event &ev)
{
	switch (ev.type)
	{
	case SDL_MOUSEMOTION:
		return ev.motion.which == kMark;       // drop 3ds-sdl's own touch events
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		return ev.button.which == kMark;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
	{
		OpenXcom::State *st = 0;
		const Ctx ctx = context(&st);
		const SDLKey k = ev.key.keysym.sym;
		const bool down = ev.type == SDL_KEYDOWN;
		if (ev.key.which == kPad) ev.key.which = 0;            // generated from the HID state: a plain button key
		else if (ev.key.which == 0 && (k == SDLK_a || k == SDLK_b || k == SDLK_x || k == SDLK_y || k == SDLK_l || k == SDLK_r
		         || k == SDLK_RETURN || k == SDLK_ESCAPE || k == SDLK_LSHIFT || k == SDLK_RSHIFT
		         || k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT))
			return false;                                      // 3ds-sdl's copy of a button we already generate
		if (down) // diagnostics (stuck-screen report): what each button press meets
			OpenXcom::Logger().get(OpenXcom::LOG_INFO) << "3ds key " << (int)k << (ev.key.which == kMark ? " (pad)" : "")
			              << " ctx " << (int)ctx << " state " << (st ? typeid(*st).name() : "-")
			              << " gameOnTop " << openxcom3dsGameOnTop();
		if (ev.key.which == kMark && (k != SDLK_UP && k != SDLK_DOWN && k != SDLK_LEFT && k != SDLK_RIGHT))
			return true;                       // keys sent by the pad / keyboard: as they are
		if (k == SDLK_ESCAPE || k == SDLK_LSHIFT)
		{
			// SELECT or ZL: game on the top screen <-> on the touch screen
			if (down) openxcom3dsToggleSwap();
			return false;
		}
		if (k == SDLK_RSHIFT && ctx == CTX_BATTLE)
		{
			// ZR: tap = view level up, hold (0.35 s) = level down. Decided on release, and sent
			// to the game as one key press
			static Uint32 zrAt = 0;
			if (down) { zrAt = SDL_GetTicks(); return false; }
			ev.type = SDL_KEYDOWN; ev.key.state = SDL_PRESSED;
			ev.key.keysym.sym = (SDL_GetTicks() - zrAt >= 350) ? OpenXcom::Options::keyBattleLevelDown : OpenXcom::Options::keyBattleLevelUp;
			return true;
		}
		if (k == SDLK_RETURN && ev.key.which != kMark)
		{
			// Start = in-game manual (the keyboard's own Enter is tagged kMark)
			if (down) runManual();
			return false;
		}
		if (k == SDLK_a || (k == SDLK_b && ctx == CTX_BATTLE))
		{
			// A = left click at the cursor; B = right click in battle (turn unit)
			postButton(k == SDLK_a ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, down);
			return false;
		}
		if (k == SDLK_b)
		{
			ev.key.keysym.sym = OpenXcom::Options::keyCancel;   // B = back everywhere else
			return true;
		}
		if (ctx == CTX_BATTLE && k == SDLK_x)
		{
			// End turn needs two presses within 2.5 s (prompt on the top screen)
			if (down)
			{
				if (g_endTurnArmed3ds) { g_endTurnArmed3ds = false; g_endTurnSentDown = true; ev.key.keysym.sym = OpenXcom::Options::keyBattleEndTurn; return true; }
				g_endTurnArmed3ds = true; g_endTurnArmedAt = SDL_GetTicks();
				return false;
			}
			if (g_endTurnSentDown) { g_endTurnSentDown = false; ev.key.keysym.sym = OpenXcom::Options::keyBattleEndTurn; return true; }
			return false;
		}
		if (down) g_endTurnArmed3ds = false;       // any other button cancels
		if (k == SDLK_r)
		{
			// Game on the top screen: R held + a touchpad tap = right click, so R's own action
			// (next soldier / zoom in) happens when it is let go, and only if no tap used it.
			if (down && openxcom3dsGameOnTop()) { g_rHeld = true; g_rUsed = false; return false; }
			if (!down && g_rHeld)
			{
				g_rHeld = false;
				if (!g_rUsed)
				{
					SDL_Event c = ev;
					remap(c, ctx);
					openxcom3dsSendKey(c.key.keysym.sym);
				}
				return false;
			}
		}
		if (ev.key.which == kMark) return true;    // C-stick arrows: always scroll, never menu focus
		if (ctx == CTX_MENU && (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT))
		{
			if (down) snapFocus(st, k == SDLK_RIGHT ? 1 : k == SDLK_LEFT ? -1 : 0,
			                        k == SDLK_DOWN ? 1 : k == SDLK_UP ? -1 : 0);
			return false;
		}
		remap(ev, ctx);
		return true;
	}
	default:
		return true;
	}
}
// ---- presenting the frame -----------------------------------------------------
// The port puts both screens on the display itself instead of SDL_Flip. (3ds-sdl draws each
// 320x200 half 1:1 in the middle of its screen with a GPU quad, so a game on the top screen had
// side bars, and it has no 800-pixel-wide mode.)
// - Top screen: with the 3D slider at 0 it runs in its 800x240 wide mode (gfxSetWide), where every
//   real column of the screen is addressed instead of each pixel being doubled, so text is sharper
//   across; with the slider up it goes back to 400x240 (the port draws no 3D, so the 3D stays off).
// - The game on the top screen fills it: 320x200 stretched to 400x240 (800x240), nearest pixel,
//   no side bars. The info panel and the manual text stay 1:1 (exactly 2:1 across in wide mode),
//   centred, so their text keeps whole pixels.
// - Touch screen: 320x200 at 1:1, 20 rows down (the touch mapping relies on this).
// The SDL surface is 320x400 RGBA8 (0xRRGGBBAA): rows 0-199 top screen, rows 200-399 touch screen.
namespace
{
bool g_presentInit = false;

// D-pad focus in the game's menus: a gold ring around the button the cursor is on (the D-pad jumps
// the cursor between buttons and A presses it). Drawn into the frame only, at the game's rows.
void drawFocusRing(SDL_Surface *scr, int y0)
{
	OpenXcom::State *st = 0;
	if (context(&st) != CTX_MENU || !st) return;
	const std::vector<OpenXcom::Surface*> &v = st->getSurfaces3ds();
	OpenXcom::Surface *hit = 0;
	for (size_t i = v.size(); i-- > 0;)          // topmost first
	{
		OpenXcom::Surface *s = v[i];
		if (!clickable(s)) continue;
		if (g_cx >= s->getX() && g_cx < s->getX() + s->getWidth() && g_cy >= s->getY() && g_cy < s->getY() + s->getHeight())
		{ hit = s; break; }
	}
	if (!hit) return;
	const Uint32 gold = 0xFFC828FF, dark = 0x302000FF;
	for (int r = 1; r <= 3; ++r)
	{
		const Uint32 c = r == 3 ? dark : gold;      // two gold pixels, a dark edge outside
		const int x1 = hit->getX() - r, y1 = hit->getY() - r;
		const int x2 = hit->getX() + hit->getWidth() - 1 + r, y2 = hit->getY() + hit->getHeight() - 1 + r;
		for (int y = std::max(0, y1); y <= std::min(kGameH - 1, y2); ++y)
		{
			Uint32 *row = (Uint32 *)((Uint8 *)scr->pixels + (y0 + y) * scr->pitch);
			if (y == y1 || y == y2)
			{
				for (int x = std::max(0, x1); x <= std::min(kGameW - 1, x2); ++x) row[x] = c;
			}
			else
			{
				if (x1 >= 0) row[x1] = c;
				if (x2 < kGameW) row[x2] = c;
			}
		}
	}
}

// One screen: out columns x 0..outW-1 (framebuffer column-major, 240 pixels per column, bottom
// row first), each taken from source column sxmap(x) (-1 = black) and source row offsets ymap.
template <typename SxFn>
void blitScreen(u8 *fb, int outW, const Uint8 *pix, const int *ymap, SxFn sxOf)
{
	for (int x = 0; x < outW; ++x)
	{
		u8 *p = fb + (x * 240 + 239) * 3;          // pixel (x, 0)
		const int sx = sxOf(x);
		if (sx < 0) { memset(fb + x * 240 * 3, 0, 240 * 3); continue; }
		const Uint8 *src = pix + sx * 4;
		for (int y = 0; y < 240; ++y, p -= 3)
		{
			const int o = ymap[y];
			if (o < 0) { p[0] = p[1] = p[2] = 0; continue; }
			const Uint32 c = *(const Uint32 *)(src + o);
			p[0] = (u8)(c >> 8); p[1] = (u8)(c >> 16); p[2] = (u8)(c >> 24);   // BGR8
		}
	}
}
}

void openxcom3dsPresent(SDL_Surface *scr, bool gameOnTop, bool ring)
{
	if (!scr || !scr->pixels || !gspHasGpuRight()) return;
	if (!g_presentInit)
	{
		g_presentInit = true;
		gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
		gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
		gfxSetDoubleBuffering(GFX_TOP, true);
		gfxSetDoubleBuffering(GFX_BOTTOM, true);
	}
	const bool wantWide = osGet3DSliderState() <= 0.0f;
	if (wantWide != gfxIsWide())              // libctru's own state (survives the HOME menu)
	{
		gfxSetWide(wantWide);
		gfxSet3D(false);
	}
	if (SDL_MUSTLOCK(scr)) SDL_LockSurface(scr);
	if (ring) drawFocusRing(scr, gameOnTop ? 0 : kGameH);
	const Uint8 *pix = (const Uint8 *)scr->pixels;
	const int pitch = scr->pitch;
	int ymap[240];

	// top screen
	u16 fw = 0, fh = 0;
	u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fw, &fh);   // fw = 240 (a column), fh = 400 or 800
	const int outW = fh;
	for (int y = 0; y < 240; ++y)
	{
		const int sy = gameOnTop ? y * kGameH / 240 : y - 20;
		ymap[y] = (sy >= 0 && sy < kGameH) ? sy * pitch : -1;
	}
	if (gameOnTop)
		blitScreen(fb, outW, pix, ymap, [outW](int x) { return x * kGameW / outW; });
	else
	{
		const int shift = outW >= 800 ? 1 : 0;
		blitScreen(fb, outW, pix, ymap, [shift](int x) { const int sx = (x >> shift) - 40; return (sx >= 0 && sx < kGameW) ? sx : -1; });
	}

	// touch screen
	fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh);      // 240 x 320
	for (int y = 0; y < 240; ++y)
	{
		const int sy = y - 20;
		ymap[y] = (sy >= 0 && sy < kGameH) ? (kGameH + sy) * pitch : -1;
	}
	blitScreen(fb, std::min((int)fh, kGameW), pix, ymap, [](int x) { return x; });

	if (SDL_MUSTLOCK(scr)) SDL_UnlockSurface(scr);
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

// Battle (the main battlescape view): game on the top screen, the battle pad
// on the touch screen. Everything else (Geoscape, bases, inventory, menus,
// popups): game on the touch screen for the stylus. SELECT flips it until the
// next change between the two.
bool openxcom3dsGameOnTop()
{
	const bool autoTop = context(0) == CTX_BATTLE;
	if ((int)autoTop != g_lastAutoTop)
	{
		g_lastAutoTop = autoTop;
		g_userFlip = false;
		OpenXcom::TopScreen3DS::invalidate();
	}
	return autoTop != g_userFlip;
}

void openxcom3dsToggleSwap()
{
	g_userFlip = !g_userFlip;
	g_pt = (hidKeysHeld() & KEY_TOUCH) ? PT_IGNORE : PT_NONE;   // a tap that swapped is not a game click
	if (g_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) postButton(SDL_BUTTON_LEFT, false);
	g_touching = false;
	click3ds::play();
	OpenXcom::TopScreen3DS::invalidate();
}

void openxcom3dsSendKey(SDLKey key)
{
	for (int i = 0; i < 2; ++i)
	{
		SDL_Event e; memset(&e, 0, sizeof(e));
		e.type = i == 0 ? SDL_KEYDOWN : SDL_KEYUP;
		e.key.state = i == 0 ? SDL_PRESSED : SDL_RELEASED;
		e.key.which = kMark;
		e.key.keysym.sym = key;
		SDL_PushEvent(&e);
	}
}

void openxcom3dsClickAt(int x, int y)
{
	moveCursor((float)x, (float)y);
	postButton(SDL_BUTTON_LEFT, true);
	postButton(SDL_BUTTON_LEFT, false);
}

extern "C" Uint8 openxcom3dsGetMouseState(int *x, int *y)
{
	if (x) *x = (int)g_cx;
	if (y) *y = (int)g_cy;
	return g_buttons;
}

extern "C" void openxcom3dsWarpMouse(Uint16 x, Uint16 y)
{
	moveCursor(x, y);
}
#endif

// ---------------------------------------------------------------------------
// Text entry: the system keyboard applet (the game's TextEdit fields have no
// keyboard to type on otherwise).
bool openxcom3dsKeyboard(const std::string &current, std::string &out)
{
	SwkbdState kb;
	char buf[96];
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 40);
	swkbdSetInitialText(&kb, current.c_str());
	swkbdSetHintText(&kb, "Enter a name");
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	const SwkbdButton b = swkbdInputText(&kb, buf, sizeof(buf));
	if (b != SWKBD_BUTTON_CONFIRM)
		return false;
	out = buf;
	return true;
}

void openxcom3dsPostReturn()
{
	for (int i = 0; i < 2; ++i)
	{
		SDL_Event e; memset(&e, 0, sizeof(e));
		e.type = i == 0 ? SDL_KEYDOWN : SDL_KEYUP;
		e.key.state = i == 0 ? SDL_PRESSED : SDL_RELEASED;
		e.key.which = kMark;                 // passes the input layer unchanged
		e.key.keysym.sym = SDLK_RETURN;
		SDL_PushEvent(&e);
	}
}
