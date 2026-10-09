/* Diablo II without Battle.net (proof of concept, branch re-bnet; RE-BNET.md at the repository root has the research).
 *
 * The front end's screens are built from one table of control descriptions (0x708d10, 0x30 bytes each: type, x, y
 * (bottom edge), w, h, hotkey, string id, cell file, click callback at +0x20, ...). Its click callbacks:
 *   main menu   Battle.net 0x435d30 (connection type 0x7795ec = 1, then the Battle.net log-on 0x435330),
 *               its gateway line 0x434600 (the realm list), Other Multiplayer 0x430c50
 *   Other Multiplayer  Open Battle.net 0x435d10 (0x7795ec = 3, 0x435330), TCP/IP Game 0x431a40 (0x7795ec = 2, then the
 *               TCP/IP screen 0x42ffe0), Cancel 0x434df0 (the main menu 0x4336c0)
 *   TCP/IP      Host Game 0x435d80, Join Game 0x431d20 (the address box), Cancel 0x430c50 (Other Multiplayer)
 * With D2_NO_BNET=1 the Battle.net, gateway and Open Battle.net entries get the TCP/IP Game callback, and the TCP/IP
 * screen's Cancel goes back to the main menu: no Battle.net connection is ever started from the menus.
 *
 * D2_JOIN_ADDR=<address>: put into the Join Game box when it opens (as the game's own last address, 0x7795f8, and the
 * edit control's text); Enter (the box's OK, 0x434790) then joins.
 * D2_BNET_LABEL / D2_OPENBNET_LABEL: other text for the two buttons (string ids 5107 and 5115, looked up through
 * 0x524a30). */
#include "w32.h"

enum {
    CONTROLS = 0x708d10, CTL_SIZE = 0x30, CTL_CALLBACK = 0x20,
    CB_TCPIP_GAME = 0x431a40, CB_TO_MAIN_MENU = 0x434df0,
    LAST_TCPIP = 0x7795f8,            /* char[0x100]: the address the Join Game box starts with (registry LastTcpIp) */
    JOIN_EDIT = 0x779958,             /* the Join Game box's edit control */
    EDIT_SET_TEXT = 0x4ffe10,         /* __fastcall (ecx = edit control, edx = ANSI text) */
    CONN_TYPE = 0x7795ec,             /* 0 single player, 1 Battle.net, 2 TCP/IP, 3 Open Battle.net */
};
static int on(void) { static int v = -1; if (v < 0) v = getenv("D2_NO_BNET") && atoi(getenv("D2_NO_BNET")); return v; }
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
    case 0x00434790: fprintf(stderr, "d2bnet: join OK pressed\n"); break;
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
    if (!init) {
        init = 1; const char *v[2] = {getenv("D2_BNET_LABEL"), getenv("D2_OPENBNET_LABEL")};
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
