#!/usr/bin/env python3
"""Apply the 3DS port edits to an OpenXcom checkout (idempotent, anchored).

Usage: patch_openxcom.py <OpenXcom repo root>

Edits (all in src/Engine/CrossPlatform.cpp + src/main.cpp):
  - 3DS data/user/config folders (romfs:/ and sdmc:/3ds/openxcom/)
  - no getpwuid / execinfo / dlfcn / system() on newlib
  - 3DS service init/exit around main()
"""
import os, shutil, sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, "..", "source")


def rw(fname, old, new, marker):
    path = os.path.join(ROOT, fname)
    t = open(path, encoding="utf-8").read()
    if marker in t:
        print("  = %-34s already patched" % fname); return
    if old not in t:
        raise SystemExit("ANCHOR NOT FOUND in %s:\n-----\n%s\n-----" % (fname, old))
    assert marker in new
    open(path, "w", encoding="utf-8").write(t.replace(old, new, 1))
    print("  + %-34s %s" % (fname, marker.strip().splitlines()[0][:40]))


CP = "src/Engine/CrossPlatform.cpp"

# includes that newlib lacks
rw(CP,
   "#include <pwd.h>\n#include <execinfo.h>\n#include <cxxabi.h>\n#include <dlfcn.h>\n",
   "#ifndef __3DS__\n#include <pwd.h>\n#include <execinfo.h>\n#include <cxxabi.h>\n#include <dlfcn.h>\n#endif\n",
   "#ifndef __3DS__\n#include <pwd.h>")

# no system()/dialogs
rw(CP,
   "#ifndef _WIN32\n\tif (system(NULL))\n",
   "#if !defined(_WIN32) && !defined(__3DS__)\n\tif (system(NULL))\n",
   "!defined(__3DS__)\n\tif (system(NULL))")

# getHome(): no getpwuid
rw(CP,
   "\tchar const *home = getenv(\"HOME\");\n\tif (!home)\n\t{\n\t\tstruct passwd *const pwd = getpwuid(getuid());\n\t\thome = pwd->pw_dir;\n\t}\n\treturn home;",
   "\tchar const *home = getenv(\"HOME\");\n#ifdef __3DS__\n\tif (!home)\n\t\thome = \"sdmc:/3ds/openxcom\";\n#else\n\tif (!home)\n\t{\n\t\tstruct passwd *const pwd = getpwuid(getuid());\n\t\thome = pwd->pw_dir;\n\t}\n#endif\n\treturn home;",
   "home = \"sdmc:/3ds/openxcom\";")

# data folders: romfs first, then the SD card
rw(CP,
   "std::vector<std::string> findDataFolders()\n{\n\tstd::vector<std::string> list;\n#ifdef __MORPHOS__\n",
   "std::vector<std::string> findDataFolders()\n{\n\tstd::vector<std::string> list;\n#ifdef __3DS__\n"
   "\tlist.push_back(\"sdmc:/3ds/openxcom/\");\n\tlist.push_back(\"romfs:/\");\n\treturn list;\n#endif\n#ifdef __MORPHOS__\n",
   "list.push_back(\"sdmc:/3ds/openxcom/\");\n\tlist.push_back(\"romfs:/\");")

# user folders / config folder: SD card
rw(CP,
   "std::vector<std::string> findUserFolders()\n{\n\tstd::vector<std::string> list;\n\n#ifdef __MORPHOS__\n",
   "std::vector<std::string> findUserFolders()\n{\n\tstd::vector<std::string> list;\n\n#ifdef __3DS__\n"
   "\tlist.push_back(\"sdmc:/3ds/openxcom/user/\");\n\treturn list;\n#endif\n#ifdef __MORPHOS__\n",
   "list.push_back(\"sdmc:/3ds/openxcom/user/\");")
rw(CP,
   "std::string findConfigFolder()\n{\n#ifdef __MORPHOS__\n\treturn \"PROGDIR:\";\n#endif\n",
   "std::string findConfigFolder()\n{\n#ifdef __3DS__\n\treturn \"sdmc:/3ds/openxcom/user/\";\n#endif\n#ifdef __MORPHOS__\n\treturn \"PROGDIR:\";\n#endif\n",
   "return \"sdmc:/3ds/openxcom/user/\";")

# stack trace: no backtrace()/dladdr on newlib
rw(CP,
   "#elif __CYGWIN__\n\tLog(LOG_FATAL) << \"Unfortunately, no stack trace information is available\";\n#else\n",
   "#elif defined(__CYGWIN__) || defined(__3DS__)\n\tLog(LOG_FATAL) << \"Unfortunately, no stack trace information is available\";\n#else\n",
   "defined(__CYGWIN__) || defined(__3DS__)")

# Screen: put the game surface on the top screen (3ds-sdl flag)

SC = "src/Engine/Screen.cpp"
# dual-screen: one 320x400 SDL surface; rows 0-199 are shown on the top screen
# (centred, 1:1), rows 200-399 on the bottom (touch) screen. No FULLSCREEN:
# on 3ds-sdl that flag stretches the surface (uneven rows).
rw(SC,
   "\t_flags = SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_HWPALETTE;\n",
   "\t_flags = SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_HWPALETTE;\n#ifdef __3DS__\n\t_flags |= SDL_DUALSCR; // 3ds-sdl: same frame on both screens, stylus on the bottom one\n#endif\n",
   "_flags |= SDL_DUALSCR;")
rw(SC,
   "\t_bpp = (use32bitScaler() || useOpenGL()) ? 32 : 8;\n\t_baseWidth = Options::baseXResolution;\n",
   "\t_bpp = (use32bitScaler() || useOpenGL()) ? 32 : 8;\n#ifdef __3DS__\n\t_flags &= ~(SDL_FULLSCREEN | SDL_NOFRAME | SDL_RESIZABLE);\n\t_bpp = 32; // let SDL convert the 8-bit frame; avoids the port's paletted path\n#endif\n\t_baseWidth = Options::baseXResolution;\n",
   "_flags &= ~(SDL_FULLSCREEN")
rw(SC,
   "\t\t_screen = SDL_SetVideoMode(width, height, _bpp, _flags);\n",
   "#ifdef __3DS__\n\t\t_screen = SDL_SetVideoMode(width, height * 2, _bpp, _flags); // top half + bottom half\n#else\n\t\t_screen = SDL_SetVideoMode(width, height, _bpp, _flags);\n#endif\n",
   "height * 2, _bpp, _flags); // top half")
rw(SC,
   "int Screen::getHeight() const\n{\n\treturn _screen->h;\n}\n",
   "int Screen::getHeight() const\n{\n#ifdef __3DS__\n\treturn _screen->h / 2; // logical height: the frame is shown twice (see flip)\n#else\n\treturn _screen->h;\n#endif\n}\n",
   "logical height: the frame is shown twice")
rw(SC,
   "\telse\n\t{\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, 0);\n\t}\n",
   "\telse\n\t{\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, 0);\n#ifdef __3DS__\n\t\tSDL_Rect lower = { 0, (Sint16)_baseHeight, 0, 0 };\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, &lower); // bottom (touch) screen copy\n#endif\n\t}\n",
   "SDL_Rect lower = { 0, (Sint16)_baseHeight, 0, 0 };")

# main(): bring up / tear down 3DS services around the game
# main(): bring up / tear down 3DS services around the game
M = "src/main.cpp"
t = open(os.path.join(ROOT, M), encoding="utf-8").read()
if "openxcom3dsInit" not in t:
    t = t.replace("int main(int argc, char *argv[])\n{\n",
                  "#ifdef __3DS__\n#include \"3ds/platform3ds.h\"\n#endif\n\n"
                  "int main(int argc, char *argv[])\n{\n#ifdef __3DS__\n\topenxcom3dsInit();\n#endif\n", 1)
    assert "openxcom3dsInit" in t, "main() anchor not found"
    open(os.path.join(ROOT, M), "w", encoding="utf-8").write(t)
    print("  + %-34s openxcom3dsInit()" % M)
else:
    print("  = %-34s already patched" % M)

# arm-none-eabi GCC defaults to -fshort-enums, so Options.h enums are 1 byte,
# but Options.cpp registers them as (int*) -- the 4-byte reset writes stomp
# the neighbouring globals (the useOpenGLShader std::string on this layout ->
# free() of a garbage pointer). Pin every Options.h enum to int.
import re
OH = "src/Engine/Options.h"
t = open(os.path.join(ROOT, OH), encoding="utf-8").read()
if ": int" not in t:
    t2 = re.sub(r"\benum (\w+)(\s*)\{", r"enum \1 : int\2{", t)
    assert t2 != t, "no enums found in Options.h"
    open(os.path.join(ROOT, OH), "w", encoding="utf-8").write(t2)
    print("  + %-34s enums pinned to int" % OH)
else:
    print("  = %-34s already patched" % OH)

# Game.cpp: circle-pad cursor + A/B clicks, and touch coordinates from the
# lower half of the surface mapped back into game space.
GM = "src/Engine/Game.cpp"
rw(GM,
   '#include "Options.h"\n#include "CrossPlatform.h"\n',
   '#include "Options.h"\n#include "CrossPlatform.h"\n#ifdef __3DS__\n#include "../3ds/platform3ds.h"\n#endif\n',
   '#include "../3ds/platform3ds.h"')
rw(GM,
   "\t\t// Process events\n\t\twhile (SDL_PollEvent(&_event))\n\t\t{\n",
   "\t\t// Process events\n#ifdef __3DS__\n\t\topenxcom3dsPumpInput();\n#endif\n\t\twhile (SDL_PollEvent(&_event))\n\t\t{\n#ifdef __3DS__\n\t\t\tif (!openxcom3dsTranslateEvent(_event)) continue;\n#endif\n",
   "openxcom3dsPumpInput();")

# ---- dual-screen layout (DS rule) ----------------------------------------
# Bottom (touch) screen: the game, unchanged. Top screen: an information panel
# drawn by src/3ds/topscreen3ds.cpp (soldier stats + minimap in battle, date /
# funds / bases on the Geoscape, controls in menus).
SH = "src/Engine/Screen.h"
rw(SH,
   "\tSurface *_surface;\n\tSDL_Rect _clear;\n",
   "\tSurface *_surface;\n\tSDL_Rect _clear;\n#ifdef __3DS__\n\tSurface *_top3ds;\n#endif\n",
   "Surface *_top3ds;")
rw(SH,
   "\t/// Renders the screen onto the game window.\n\tvoid flip();\n",
   "\t/// Renders the screen onto the game window.\n\tvoid flip();\n#ifdef __3DS__\n\t/// The 320x200 top-screen panel (created on first use).\n\tSurface *getTopSurface3ds();\n#endif\n",
   "Surface *getTopSurface3ds();")
rw(SC,
   "Screen::~Screen()\n{\n\tdelete _surface;\n}\n",
   "Screen::~Screen()\n{\n\tdelete _surface;\n#ifdef __3DS__\n\tdelete _top3ds;\n#endif\n}\n\n#ifdef __3DS__\nSurface *Screen::getTopSurface3ds()\n{\n\tif (!_top3ds) _top3ds = new Surface(_baseWidth, _baseHeight, 0, 0, 8);\n\treturn _top3ds;\n}\n#endif\n",
   "Surface *Screen::getTopSurface3ds()")
rw(SC,
   "_pushPalette(false), _surface(0)\n{\n",
   "_pushPalette(false), _surface(0)\n{\n#ifdef __3DS__\n\t_top3ds = 0;\n#endif\n",
   "\t_top3ds = 0;\n")
# flip: rows 0-199 = top panel, rows 200-399 = game (touch screen)
rw(SC,
   "\telse\n\t{\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, 0);\n#ifdef __3DS__\n\t\tSDL_Rect lower = { 0, (Sint16)_baseHeight, 0, 0 };\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, &lower); // bottom (touch) screen copy\n#endif\n\t}\n",
   "\telse\n\t{\n#ifdef __3DS__\n\t\tif (_top3ds && _top3ds->getWidth() == _surface->getWidth())\n\t\t\tSDL_BlitSurface(_top3ds->getSurface(), 0, _screen, 0); // top screen: info panel\n\t\telse\n\t\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, 0);\n\t\tSDL_Rect lower = { 0, (Sint16)_baseHeight, 0, 0 };\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, &lower); // bottom (touch) screen: the game\n#else\n\t\tSDL_BlitSurface(_surface->getSurface(), 0, _screen, 0);\n#endif\n\t}\n",
   "// top screen: info panel")
STH = "src/Engine/State.h"
rw(STH,
   "public:\n\t/// Creates a new state linked to a game.\n",
   "public:\n#ifdef __3DS__\n\tconst std::vector<Surface*> &getSurfaces3ds() const { return _surfaces; }\n\tstatic Game *game3ds() { return _game; }\n#endif\n\t/// Creates a new state linked to a game.\n",
   "getSurfaces3ds()")
GH = "src/Engine/Game.h"
rw(GH,
   "\t/// Gets the game's display screen.\n\tScreen *getScreen() const;\n",
   "\t/// Gets the game's display screen.\n\tScreen *getScreen() const;\n#ifdef __3DS__\n\tState *getTopState3ds() const { return _states.empty() ? 0 : _states.back(); }\n#endif\n",
   "getTopState3ds()")
rw(GM,
   '#include "../3ds/platform3ds.h"\n#endif\n',
   '#include "../3ds/platform3ds.h"\n#include "../3ds/topscreen3ds.h"\n#endif\n',
   '#include "../3ds/topscreen3ds.h"')
rw(GM,
   "\t\t\t\t_screen->flip();\n",
   "#ifdef __3DS__\n\t\t\t\tTopScreen3DS::render(this, _screen->getTopSurface3ds());\n#endif\n\t\t\t\t_screen->flip();\n",
   "TopScreen3DS::render(this")

# Mod.cpp: a missing music track plays silence instead of being fatal
# (the Geoscape asks for GMGEO1 by id; with no playable format it threw).
MOD = "src/Mod/Mod.cpp"
rw(MOD,
   "\t\t\tss << name << id;\n\t\t\tmusic = getMusic(ss.str());\n",
   "\t\t\tss << name << id;\n\t\t\tmusic = getMusic(ss.str(), false);\n\t\t\tif (!music) music = _muteMusic; // 3DS: missing track = silence, not a crash\n",
   "missing track = silence, not a crash")

# TextEdit.cpp: when a text field takes focus, type into it with the 3DS
# system keyboard, then press Enter for the player (accepts the dialog).
TE = "src/Interface/TextEdit.cpp"
rw(TE,
   '#include "TextEdit.h"\n',
   '#include "TextEdit.h"\n#ifdef __3DS__\n#include "../3ds/platform3ds.h"\n#endif\n',
   '#include "../3ds/platform3ds.h"')
rw(TE,
   "\t\t\t_timer->start();\n\t\t\tif (_modal)\n\t\t\t\t_state->setModal(this);\n",
   "\t\t\t_timer->start();\n\t\t\tif (_modal)\n\t\t\t\t_state->setModal(this);\n"
   "#ifdef __3DS__\n\t\t\t{\n\t\t\t\tstd::string typed;\n"
   "\t\t\t\tif (openxcom3dsKeyboard(getText(), typed))\n\t\t\t\t{\n"
   "\t\t\t\t\tsetText(typed);\n\t\t\t\t\t_caretPos = _value.length();\n"
   "\t\t\t\t\topenxcom3dsPostReturn(); // 3DS keyboard: accept\n\t\t\t\t}\n\t\t\t}\n#endif\n",
   "3DS keyboard: accept")

os.makedirs(os.path.join(ROOT, "src", "3ds"), exist_ok=True)
for f in ("platform3ds.h", "platform3ds.cpp", "scalers_stub.cpp", "topscreen3ds.h", "topscreen3ds.cpp", "mouse3ds.h"):
    shutil.copy(os.path.join(PORT, "3ds", f), os.path.join(ROOT, "src", "3ds", f))
    print("  + src/3ds/%-25s copied" % f)
shutil.copy(os.path.join(PORT, "Makefile.3ds"), os.path.join(ROOT, "src", "Makefile.3ds"))
print("  + src/Makefile.3ds                 copied")
print("done")
