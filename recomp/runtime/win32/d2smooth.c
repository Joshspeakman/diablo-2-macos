/* Diablo II at more frames than its 25 game ticks a second (D2_FPS=<frames per second>, the launch window's Frame
 * Rate; runtime/win32/hooks.c calls in here from hooks tools/lift.py places in the client's frame function).
 *
 * The game moves its units once a tick (40 ms) and draws once a tick in single player. Here it draws on every pass of
 * its idle loop instead, and each draw between two ticks shows the moving units (players, monsters, missiles) where
 * they are between their positions at the last two ticks: their paths' positions are set to the blend just before the
 * game draws and put back right after, so only the picture sees them (the game's logic, saves and network never do).
 * The camera follows the player's drawn position, so the view scrolls smoothly too. Animation frames stay at 25 a
 * second, as the game advances them in its ticks. Frames are paced to the rate chosen at the game's draw decision, so
 * its ticks are never held back by a wait for the next frame. */
#include "w32.h"
#include <time.h>
#include <unistd.h>

/* 1.14d: the client's units, two sets of hash tables (6 unit types x 128 buckets, chained through unit+0xe4) */
enum { UNIT_TABLES_A = 0x7a5e70, UNIT_TABLES_B = 0x7a5270, PLAYER_UNIT = 0x7a6a70, TICK_PERIOD = 0x70ef1c };
enum { U_TYPE = 0x00, U_ID = 0x0c, U_PATH = 0x2c, U_NEXT = 0xe4 };
/* a dynamic path (players 0, monsters 1, missiles 3): position in 1/65536 subtiles (+0, +4) and the same position in
 * the client's pixels (+8 = (x - y) * 16, +c = (x + y) * 8), which is what the drawing reads */
enum { P_X = 0x00, P_Y = 0x04, P_PX = 0x08, P_PY = 0x0c };

typedef struct { uint32_t unit, id, path; int32_t prev[4], cur[4], shown[4]; int live; } Track;
#define NTRACK 4096
static Track tr[NTRACK];
static int drawn[NTRACK], ndrawn;                                 /* the tracks set for the draw under way */
static uint32_t ticks, snap_ticks; static double tick_ms;
static int enabled = -1;

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int d2smooth_on(void)
{
    if (enabled < 0) { const char *e = getenv("D2_FPS"); enabled = e && atoi(e) > 25; }
    return enabled;
}
/* the game's draw decision, each pass of its idle loop in game: draw when the next frame is due, else skip the draw and
 * wait a moment (the next pass may bring a tick, which then runs on time) */
int d2smooth_frame_due(void)
{
    static double next; static double step;
    if (!step) { const char *e = getenv("D2_FPS"); int f = e ? atoi(e) : 60; step = 1000.0 / (f > 25 ? f : 60); }
    double now = now_ms();
    if (now + 0.25 < next) { usleep(next - now > 1.5 ? 1000 : 250); return 0; }
    next = next > now - step ? next + step : now + step;              /* steady cadence; after a stall, from now */
    return 1;
}
/* the client's game tick has run (hooked at its start; the positions it sets are read at the next draw) */
void d2smooth_tick(void) { if (d2smooth_on()) { ticks++; tick_ms = now_ms(); } }

static Track *track_of(uint32_t unit, uint32_t id)
{
    uint32_t h = (unit >> 4) * 2654435761u >> 20;
    for (int k = 0; k < 64; k++) {
        Track *t = &tr[(h + (uint32_t)k) & (NTRACK - 1)];
        if (!t->unit || (t->unit == unit && t->id == id)) return t;
        if (!t->live) return t;                                       /* a unit gone since: its slot is reused */
    }
    return 0;
}
static void read4(uint32_t path, int32_t *v) { for (int k = 0; k < 4; k++) v[k] = (int32_t)rt_r32(G_MEM, path + 4 * (uint32_t)k); }
static void write4(uint32_t path, const int32_t *v) { for (int k = 0; k < 4; k++) rt_w32(G_MEM, path + 4 * (uint32_t)k, (uint32_t)v[k]); }

/* before the game draws: the moving units at their blended positions */
void d2smooth_before_draw(void)
{
    if (!d2smooth_on() || !rt_r32(G_MEM, PLAYER_UNIT)) return;
    int newtick = ticks != snap_ticks; snap_ticks = ticks; ndrawn = 0;
    if (newtick) for (int k = 0; k < NTRACK; k++) tr[k].live = 0;
    uint32_t period = rt_r32(G_MEM, TICK_PERIOD); if (period < 10 || period > 200) period = 40;
    double a = (now_ms() - tick_ms) / period; if (a < 0) a = 0; if (a > 1) a = 1;
    static const uint32_t tables[2] = {UNIT_TABLES_A, UNIT_TABLES_B};
    for (int tb = 0; tb < 2; tb++) for (uint32_t type = 0; type < 4; type++) {
        if (type == 2) continue;                                      /* objects stand still */
        for (uint32_t b = 0; b < 128; b++) {
            uint32_t u = rt_r32(G_MEM, tables[tb] + type * 0x200 + b * 4);
            for (int guard = 0; u && guard < 4096; guard++, u = rt_r32(G_MEM, u + U_NEXT)) {
                uint32_t path = rt_r32(G_MEM, u + U_PATH), id = rt_r32(G_MEM, u + U_ID);
                if (!path || rt_r32(G_MEM, u + U_TYPE) != type) continue;
                Track *t = track_of(u, id); if (!t) continue;
                int32_t real[4]; read4(path, real);
                if (t->unit != u || t->id != id || t->path != path) {   /* new to us: no motion to blend yet */
                    t->unit = u; t->id = id; t->path = path; memcpy(t->prev, real, 16); memcpy(t->cur, real, 16);
                } else if (newtick) { memcpy(t->prev, t->cur, 16); memcpy(t->cur, real, 16); }
                else memcpy(t->cur, real, 16);                          /* (the game moved it without a tick: follow) */
                t->live = 1;
                int32_t dx = t->cur[0] - t->prev[0], dy = t->cur[1] - t->prev[1];
                if (dx > 0x40000 || dx < -0x40000 || dy > 0x40000 || dy < -0x40000) { memcpy(t->prev, t->cur, 16); }   /* a jump (teleport, town portal): no glide */
                for (int k = 0; k < 4; k++) t->shown[k] = t->prev[k] + (int32_t)lround((double)(t->cur[k] - t->prev[k]) * a);
                write4(path, t->shown); drawn[ndrawn++] = (int)(t - tr);
            }
        }
    }
}
/* after the game has drawn: the real positions back (unless the drawing itself moved one) */
void d2smooth_after_draw(void)
{
    if (!d2smooth_on()) return;
    for (int k = 0; k < ndrawn; k++) {
        Track *t = &tr[drawn[k]];
        int32_t now[4]; read4(t->path, now);
        if (!memcmp(now, t->shown, 16)) write4(t->path, t->cur);
    }
    ndrawn = 0;
}
