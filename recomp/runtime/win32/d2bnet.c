/* Diablo II without Battle.net (the research: docs/RE-BNET.md). Blizzard's servers won't take this copy, so the menus'
 * Battle.net buttons lead to the game's own TCP/IP games instead, with the serverless game list (d2net.c) in Internet
 * mode (the launch window's Multiplayer). D2_BNET=1 leaves the buttons as they were.
 *
 * The front end's screens are built from one table of control descriptions (0x708d10, 0x30 bytes each: type, x, y
 * (bottom edge), w, h, hotkey, string id, cell file, click callback at +0x20, ...). Its click callbacks:
 *   main menu   Battle.net 0x435d30 (connection type 0x7795ec = 1, then the Battle.net log-on 0x435330),
 *               its gateway line 0x434600 (the realm list), Other Multiplayer 0x430c50
 *   Other Multiplayer  Open Battle.net 0x435d10 (0x7795ec = 3, 0x435330), TCP/IP Game 0x431a40 (0x7795ec = 2, then the
 *               TCP/IP screen 0x42ffe0), Cancel 0x434df0 (the main menu 0x4336c0)
 *   TCP/IP      Host Game 0x435d80, Join Game 0x431d20 (the address box), Cancel 0x430c50 (Other Multiplayer)
 * The Battle.net, gateway and Open Battle.net entries get the TCP/IP Game callback, and the TCP/IP screen's Cancel goes
 * back to the main menu: no Battle.net connection is ever started from the menus. The gateway line ("GATEWAY: U.S.
 * WEST") is hidden. The two buttons read ONLINE GAMES (Internet) or LAN GAMES (LAN).
 *
 * D2_JOIN_ADDR=<address>: put into the Join Game box when it opens (as the game's own last address, 0x7795f8, and the
 * edit control's text); Enter (the box's OK, 0x434790) then joins.
 * D2_BNET_LABEL / D2_OPENBNET_LABEL: other text for the two buttons (string ids 5107 and 5115, looked up through
 * 0x524a30).
 * Internet: the Join Game box opening shows the game list over it (d2net_join_screen); its OK or Cancel hides it.
 * d2net_host_info: the hosted game's details for the list, from the front end's game record and the game's roster. */
#include "w32.h"
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

enum {
    CONTROLS = 0x708d10, CTL_SIZE = 0x30, CTL_CALLBACK = 0x20,
    CB_TCPIP_GAME = 0x431a40, CB_TO_MAIN_MENU = 0x434df0,
    LAST_TCPIP = 0x7795f8,            /* char[0x100]: the address the Join Game box starts with (registry LastTcpIp) */
    JOIN_EDIT = 0x779958,             /* the Join Game box's edit control */
    EDIT_SET_TEXT = 0x4ffe10,         /* __fastcall (ecx = edit control, edx = ANSI text) */
    CONN_TYPE = 0x7795ec,             /* 0 single player, 1 Battle.net, 2 TCP/IP, 3 Open Battle.net */
};
static int on(void) { static int v = -1; if (v < 0) { const char *b = getenv("D2_BNET"); v = !(b && atoi(b)); } return v; }
static int lan(void) { const char *m = getenv("D2_MULTIPLAYER"); return m && !strcmp(m, "lan"); }
static void set_callback(uint32_t id, uint32_t cb)
{
    uint32_t at = CONTROLS + id * CTL_SIZE + CTL_CALLBACK;
    fprintf(stderr, "d2bnet: control 0x%x callback 0x%08x -> 0x%08x\n", id, rt_r32(G_MEM, at), cb);
    rt_w32(G_MEM, at, cb);
}
/* start-up (the 0x4065af hook): before any menu is built */
void d2bnet_startup(void)
{
    if (!on()) return;
    set_callback(0x0a, CB_TCPIP_GAME); set_callback(0x11, CB_TCPIP_GAME);       /* Battle.net (classic, expansion) */
    set_callback(0x0b, CB_TCPIP_GAME); set_callback(0x12, CB_TCPIP_GAME);       /* its gateway line */
    set_callback(0xff, CB_TCPIP_GAME);                                          /* Open Battle.net */
    set_callback(0x10a, CB_TO_MAIN_MENU);                                       /* TCP/IP screen: Cancel */
    /* the gateway line ("GATEWAY: U.S. WEST"): still made (the menu sets its text through it), but off the screen */
    rt_w32(G_MEM, CONTROLS + 0x0b * CTL_SIZE + 4, 4000); rt_w32(G_MEM, CONTROLS + 0x12 * CTL_SIZE + 4, 4000);
}

static char join_addr[64]; static volatile int join_pending;
/* the host app's game list: the address to join, used when the box opens next (any thread) */
void d2bnet_set_join_address(const char *a) { snprintf(join_addr, sizeof join_addr, "%s", a ? a : ""); join_pending = join_addr[0] != 0; }
/* hook 0x431de9: the Join Game box is built (its edit control at JOIN_EDIT, text from LAST_TCPIP) */
void d2bnet_join_box(Ctx *c)
{
    static int env_done; const char *e = getenv("D2_JOIN_ADDR");
    if (!env_done && e && *e) { env_done = 1; d2bnet_set_join_address(e); }
    uint32_t edit = rt_r32(G_MEM, JOIN_EDIT);
    fprintf(stderr, "d2bnet: join box open (edit control 0x%08x, last address \"%.63s\")\n", edit, (char *)G_MEM + LAST_TCPIP);
    if (!join_pending) d2net_join_screen(1);                                   /* Internet: the game list over the box */
    if (!join_pending || !edit) return;
    join_pending = 0;
    static uint32_t buf; if (!buf) buf = heap_alloc(w32_process_heap, 8, 64); if (!buf) return;
    snprintf((char *)G_MEM + buf, 64, "%s", join_addr);
    snprintf((char *)G_MEM + LAST_TCPIP, 0x100, "%s", join_addr);
    uint32_t eax = c->eax, ecx = c->ecx, edx = c->edx; LFs f = c->f;
    c->ecx = edit; c->edx = buf; w32_callback(c, EDIT_SET_TEXT, 0, 0);
    c->eax = eax; c->ecx = ecx; c->edx = edx; c->f = f;
    fprintf(stderr, "d2bnet: join box filled with \"%s\"\n", join_addr);
}
void d2bnet_event(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case 0x00434790: fprintf(stderr, "d2bnet: join OK pressed\n"); d2net_join_screen(0); break;
    case 0x00431e00: d2net_join_screen(0); break;                                /* the box's Cancel */
    case 0x004348a8: fprintf(stderr, "d2bnet: join failed (no connection)\n"); break;
    case 0x0043490e: fprintf(stderr, "d2bnet: joined: connection type %u\n", rt_r32(G_MEM, CONN_TYPE)); break;
    case 0x00435d80: fprintf(stderr, "d2bnet: host game chosen\n"); break;
    case 0x0042ffe0: fprintf(stderr, "d2bnet: TCP/IP screen (connection type %u)\n", rt_r32(G_MEM, CONN_TYPE)); break;
    }
    (void)c;
}
/* override 0x524a30 (the string table lookup: __fastcall ecx = id, returns a UTF-16 string): other button labels */
int d2bnet_string(Ctx *c)
{
    static int init; static uint32_t lab[2]; uint16_t id = (uint16_t)c->ecx;
    if (!on()) return 0;
    if (!init) {
        const char *dflt = lan() ? "LAN GAMES" : "ONLINE GAMES";
        init = 1; const char *v[2] = {getenv("D2_BNET_LABEL") ?: dflt, getenv("D2_OPENBNET_LABEL") ?: dflt};
        for (int k = 0; k < 2; k++) if (v[k] && *v[k]) {
            size_t n = strlen(v[k]); if (n > 60) n = 60;
            if (!(lab[k] = heap_alloc(w32_process_heap, 8, (uint32_t)(2 * n + 2)))) continue;
            for (size_t j = 0; j <= n; j++) rt_w16(G_MEM, lab[k] + 2 * (uint32_t)j, j < n ? (uint8_t)v[k][j] : 0);
        }
    }
    uint32_t s = id == 5107 ? lab[0] : id == 5115 ? lab[1] : 0;
    if (!s) return 0;
    c->eax = s; c->esp += 4;                                    /* ret */
    return 1;
}

/* ---- the hosted game's details for the game list (d2net.c asks from its own thread) ---- */
enum { GAME_RECORD = 0x7795d4, ROSTER = 0x7bb5c0 };
/* a guest dword the game may be changing, read without risking a fault on a stale pointer */
static int peek32(uint32_t a, uint32_t *v)
{
    if (a < 0x10000u || a > 0xfffffff0u) return 0;
#ifdef __APPLE__
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)(G_MEM + a), 4, (mach_vm_address_t)(uintptr_t)v, &got) == KERN_SUCCESS && got == 4;
#else
    memcpy(v, G_MEM + a, 4); return 1;
#endif
}
static int peek_bytes(uint32_t a, void *out, uint32_t n)
{
    for (uint32_t k = 0; k < n; k += 4) { uint32_t v; if (!peek32(a + k, &v)) return 0; memcpy((uint8_t *)out + k, &v, n - k < 4 ? n - k : 4); }
    return 1;
}
int d2net_host_info(char *name, int cap, int *cls, int *level, int *difficulty, int *players)
{
    uint32_t b; char nm[17] = {0};
    if (!peek32(GAME_RECORD, &b) || !b || !peek_bytes(b + 0xbd, nm, 16)) { snprintf(name, (size_t)cap, "Diablo II game"); return 0; }
    for (int k = 0; k < 16 && nm[k]; k++) if ((uint8_t)nm[k] < 32 || (uint8_t)nm[k] >= 127) nm[k] = '?';
    snprintf(name, (size_t)cap, "%s", nm);
    uint32_t v; if (peek32(b + 0x1ec, &v)) *cls = (int)((v >> 16) & 0xff) % 7;          /* byte +0x1ee */
    if (peek32(b + 0x210, &v)) *difficulty = (int)(v & 0xff) % 3;                        /* byte +0x210 */
    /* the roster (all players in the game): entries name[16] ... +0x20 level (word), +0x80 next */
    uint32_t e; int n = 0;
    if (peek32(ROSTER, &e)) for (; e && n < 8; n++) {
        char en[17] = {0}; uint32_t lv, next;
        if (!peek_bytes(e, en, 16) || !peek32(e + 0x20, &lv) || !peek32(e + 0x80, &next)) break;
        if (!strncmp(en, nm, 16) && (lv & 0xffff) <= 99) *level = (int)(lv & 0xffff);
        e = next;
    }
    *players = n ? n : 1;
    return 1;
}
