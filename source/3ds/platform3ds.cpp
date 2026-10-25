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
	static const char *kCfgVersion = "4\n";
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
			      "  dragScrollButton: 1\n", f);
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
namespace
{
const int kGameW = 320, kGameH = 200;
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
		else if (k == SDLK_x) k = OpenXcom::Options::keyBattleEndTurn;
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
	if (hidKeysHeld() & KEY_TOUCH)
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
