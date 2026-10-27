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
const Uint8 kMark = 0x3D;                     // "which" tag on events we post
// touch -> game space: the game is drawn 1:1, centred vertically (y 20..219)
const int kTouchOffsetX = 0, kTouchOffsetY = 20;

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
		else if (k == SDLK_LSHIFT) k = OpenXcom::Options::keyBattleLevelDown;
		else if (k == SDLK_RSHIFT) k = OpenXcom::Options::keyBattleLevelUp;
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

void runManual()
{
	if (!g_manualLoaded)
	{
		g_manual.load(kManualText, "sdmc:/3ds/openxcom/manual.txt", "X-COM for 3DS: manual");
		g_manualLoaded = true;
	}
	SDL_Surface *scr = SDL_GetVideoSurface();
	if (!scr) return;
	// release any held mouse button so the game doesn't see a stuck click
	if (g_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) postButton(SDL_BUTTON_LEFT, false);
	if (g_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) postButton(SDL_BUTTON_RIGHT, false);
	g_touching = false;

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
		const u32 down = hidKeysDown(), rep = hidKeysDownRepeat();
		manual3ds::Input mi;
		static const struct { u32 b; uint32_t k; bool rep; } map[] = {
			{ KEY_DUP, manual3ds::K_UP, true }, { KEY_DDOWN, manual3ds::K_DOWN, true },
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
			mi.tx = tp.px - kTouchOffsetX;
			mi.ty = tp.py - kTouchOffsetY;     // the 320x200 bottom half is centred
		}
		circlePosition cp;
		hidCircleRead(&cp);
		mi.cpad_y = cp.dy;
		const bool wasDirty = g_manual.dirty();
		if (!g_manual.update(mi)) break;
		if (redraw || mi.down || mi.touch_down || wasDirty || g_manual.dirty())
		{
			g_manual.draw_top(top);
			g_manual.draw_bottom(bot);
			blitCanvas(scr, top, 0);
			blitCanvas(scr, bot, half);
			SDL_Flip(scr);
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
	// touch screen, straight from the hardware
	if (g_touchBlocked)
	{
		if (!(hidKeysHeld() & KEY_TOUCH)) g_touchBlocked = false;
	}
	else if (hidKeysHeld() & KEY_TOUCH)
	{
		touchPosition tp;
		hidTouchRead(&tp);
		if (tp.px || tp.py)
		{
			moveCursor((float)(tp.px - kTouchOffsetX), (float)(tp.py - kTouchOffsetY));
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
