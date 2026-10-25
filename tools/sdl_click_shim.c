// LD_PRELOAD shim: injects mouse clicks into SDL 1.2's event queue on a
// schedule so OpenXcom can be driven headlessly (dummy video driver).
// Schedule: XCOM_CLICKS="t:x:y,t:x:y,..." (t = seconds after start, base
// 320x200 coords, scaled by XCOM_SCALE).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL/SDL.h>

static int (*real_poll)(SDL_Event *) = NULL;
static struct { double t; int x, y; int done; } clicks[64];
static int nclicks = 0, inited = 0;
static struct timespec t0;

static void init(void) {
    inited = 1;
    real_poll = dlsym(RTLD_NEXT, "SDL_PollEvent");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const char *s = getenv("XCOM_CLICKS");
    if (!s) return;
    char *buf = strdup(s), *tok = strtok(buf, ",");
    while (tok && nclicks < 64) {
        double t; int x, y;
        if (sscanf(tok, "%lf:%d:%d", &t, &x, &y) == 3) { clicks[nclicks].t = t; clicks[nclicks].x = x; clicks[nclicks].y = y; nclicks++; }
        tok = strtok(NULL, ",");
    }
}

int SDL_PollEvent(SDL_Event *ev) {
    if (!inited) init();
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    double el = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
    int scale = getenv("XCOM_SCALE") ? atoi(getenv("XCOM_SCALE")) : 2;
    for (int i = 0; i < nclicks; ++i) {
        if (!clicks[i].done && el >= clicks[i].t) {
            clicks[i].done = 1;
            SDL_Event e; memset(&e, 0, sizeof(e));
            e.type = SDL_MOUSEMOTION; e.motion.x = clicks[i].x * scale; e.motion.y = clicks[i].y * scale; SDL_PushEvent(&e);
            memset(&e, 0, sizeof(e));
            e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED;
            e.button.x = clicks[i].x * scale; e.button.y = clicks[i].y * scale; SDL_PushEvent(&e);
            e.type = SDL_MOUSEBUTTONUP; e.button.state = SDL_RELEASED; SDL_PushEvent(&e);
            fprintf(stderr, "[shim] click %d at %.1fs (%d,%d)\n", i, el, clicks[i].x, clicks[i].y);
        }
    }
    // XCOM_DUMPS="t,t,..." -> dump the video surface as PPM at those times
    static double dumps[16]; static int ndumps = -1, dumped = 0;
    if (ndumps < 0) {
        ndumps = 0;
        const char *d = getenv("XCOM_DUMPS");
        if (d) { char *b = strdup(d), *k = strtok(b, ","); while (k && ndumps < 16) { dumps[ndumps++] = atof(k); k = strtok(NULL, ","); } }
    }
    if (dumped < ndumps && el >= dumps[dumped]) {
        SDL_Surface *s = SDL_GetVideoSurface();
        if (s) {
            char path[256]; snprintf(path, sizeof(path), "%s/xcom_%d.ppm", getenv("XCOM_OUT") ? getenv("XCOM_OUT") : ".", dumped);
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
                SDL_LockSurface(s);
                for (int y = 0; y < s->h; ++y) for (int x = 0; x < s->w; ++x) {
                    unsigned char *p = (unsigned char *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
                    Uint32 v = 0; memcpy(&v, p, s->format->BytesPerPixel);
                    Uint8 r, g, bb; SDL_GetRGB(v, s->format, &r, &g, &bb);
                    fputc(r, f); fputc(g, f); fputc(bb, f);
                }
                SDL_UnlockSurface(s);
                fclose(f);
                fprintf(stderr, "[shim] dumped %s\n", path);
            }
        }
        dumped++;
    }
    return real_poll(ev);
}
