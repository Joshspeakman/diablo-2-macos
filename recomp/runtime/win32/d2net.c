/* Online games without Battle.net: a serverless game list, as the Delta Force ports' NovaWorld stand-in.
 *
 * Diablo II's TCP/IP games are hosted by a player's own game (TCP 4000) and joined by address. This adds the list:
 *  - Hosting: when the game listens on TCP 4000 (winsock.c), the game is announced under the list's key on BitTorrent's
 *    public DHT (p2p_dht.c; D2_NO_P2P=1: local network only), the router forwards TCP 4000 and UDP 4000 (portmap.c;
 *    DS_NO_PORTMAP=1 leaves it alone), and UDP 4000 answers the list's questions with the game's details (the host's
 *    character, class, level, difficulty, players; d2net_host_info, from the game's memory).
 *  - Joining: d2net_search() asks the hosts the DHT knows, the local network (broadcast) and this Mac, every second;
 *    d2net_games() is what answered in the last 10 seconds, for the host app's list (the Join Game screen).
 * The launch window's Multiplayer row picks this (Internet) or the game's own TCP/IP as it was (LAN: D2_MULTIPLAYER=lan,
 * which leaves all of this off: no list, no DHT, no router, nothing listening on UDP 4000).
 * A group name (D2_P2P_GROUP, the launch window's Game List row) makes a private list: it is part of the DHT key, and
 * hosts answer only questions that carry its hash, so players without it neither find nor see the game.
 * Every packet is fixed-size and checked; an answer is never larger than the question (no use as an amplifier), and
 * a host answers at most 50 questions a second. */
#include "w32.h"
#include "p2p_dht.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { D2NET_PORT = 4000, PKT = 48, MAXGAMES = 64 };
static const char MAGIC[4] = {'D', '2', 'G', 'L'};
/* packet: magic[4] version(1) kind(1: 'Q' question, 'A' answer) pad[2] nonce[4] group[8], then for answers:
 * players(1) max(1) difficulty(1) class(1) level(2, LE) name[16] (zero-padded); 48 bytes either way */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static P2pDht *dht; static int hosting, host_fd = -1, search_fd = -1, searching;
static pthread_t host_th, search_th;
static uint8_t group_hash[8], host_group[8]; static int group_ready;   /* host_group: the hosted game's, fixed when hosting starts */
static D2NetGame games[MAXGAMES]; static int ngames;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }
static void group_init(void)
{
    if (group_ready) return;
    const char *g = getenv("D2_P2P_GROUP"); char buf[300];
    snprintf(buf, sizeof buf, "Diablo II game list group:%s", g ? g : "");
    uint8_t h[20]; p2p_sha1(buf, (unsigned)strlen(buf), h); memcpy(group_hash, h, 8); group_ready = 1;
}
/* test mode never reaches the public DHT or the router: only a fake DHT a test names (P2P_DHT_BOOTSTRAP) */
static int testing(void) { const char *t = getenv("DS_TEST"); return t && atoi(t); }
static int router_ok(void) { return !getenv("DS_NO_PORTMAP") && !testing(); }
static P2pDht *p2p(void)
{
    const char *off = getenv("D2_NO_P2P");
    if (testing() && !getenv("P2P_DHT_BOOTSTRAP")) return 0;
    if (!dht && !(off && atoi(off))) {
        const char *g = getenv("D2_P2P_GROUP"); char name[300];
        snprintf(name, sizeof name, "Diablo II / game list / 1%s%s", g && *g ? " / " : "", g && *g ? g : "");
        uint8_t key[20]; p2p_key(name, key); dht = p2p_dht_start(key, 0);
    }
    return dht;
}
static int udp_socket(uint16_t port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0); if (s < 0) return -1;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    fcntl(s, F_SETFD, FD_CLOEXEC);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
    return s;
}

static int list_on(void) { const char *m = getenv("D2_MULTIPLAYER"); return !(m && !strcmp(m, "lan")); }
void (*w32_host_game_list)(int open);
void d2net_join_screen(int open) { if (list_on() && w32_host_game_list) w32_host_game_list(open); }

/* ---- this Mac's public address (Internet): what the game's Host Game screen shows players to type ---- */
static uint32_t pub_cached;
uint32_t d2net_public_ip(void)
{
    if (!list_on()) return 0;
    uint32_t portmap_public_ip(void), ip = 0;
    if (testing() && getenv("D2_TEST_PUBLIC_IP")) return inet_addr(getenv("D2_TEST_PUBLIC_IP"));   /* tests: a stand-in public address */
    if (router_ok()) ip = portmap_public_ip();          /* the router's (NAT-PMP / UPnP), cached */
    if (!ip) { pthread_mutex_lock(&lock); P2pDht *d = dht; pthread_mutex_unlock(&lock); if (d) ip = p2p_dht_external_ip(d); }   /* as the DHT sees it */
    if (ip) pub_cached = ip;
    return ip ? ip : pub_cached;
}
static void *start_thread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&lock); P2pDht *d = p2p(); pthread_mutex_unlock(&lock);
    if (d) p2p_dht_search(d, 0);                                      /* one lookup: its answers also tell this node's address */
    for (int k = 0; k < 40 && !d2net_public_ip(); k++) usleep(250000);
    uint32_t ip = d2net_public_ip(); char a[32] = "not known yet"; if (ip) inet_ntop(AF_INET, &ip, a, sizeof a);
    fprintf(stderr, "d2net: Internet games: this Mac's public address is %s (players join it on port %d)\n", a, D2NET_PORT);
    return 0;
}
/* at start (Internet): learn the public address in the background, so the Host Game screen can show it */
void d2net_start(void)
{
    static int done; if (done++ || !list_on()) return;
    group_init();
    void portmap_cleanup(void); if (router_ok()) portmap_cleanup();   /* a forward an earlier run left on the router */
    pthread_t t; if (!pthread_create(&t, 0, start_thread, 0)) pthread_detach(t);
}

/* ---- hosting: answer the list's questions ---- */
#ifndef D2NET_TEST
__attribute__((weak)) int d2net_host_info(char *name, int cap, int *cls, int *level, int *difficulty, int *players)
{   /* the game's own details (d2netinfo.c, from its memory); without them the list shows a plain entry */
    (void)cls; (void)level; (void)difficulty; (void)players; snprintf(name, (size_t)cap, "Diablo II game"); return 0;
}
#else
int d2net_host_info(char *name, int cap, int *cls, int *level, int *difficulty, int *players);
#endif
static void *host_thread(void *arg)
{
    (void)arg; double window = now_s(); int answered = 0;
    for (;;) {
        pthread_mutex_lock(&lock); int fd = host_fd, on = hosting; pthread_mutex_unlock(&lock);
        if (!on || fd < 0) break;
        struct pollfd p = {fd, POLLIN, 0}; if (poll(&p, 1, 500) <= 0) continue;
        uint8_t q[PKT + 16]; struct sockaddr_in from; socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, q, sizeof q, 0, (struct sockaddr *)&from, &fl);
        if (n != PKT || memcmp(q, MAGIC, 4) || q[4] != 1 || q[5] != 'Q' || memcmp(q + 12, host_group, 8)) continue;
        double t = now_s(); if (t - window >= 1) { window = t; answered = 0; }
        if (++answered > 50) continue;                                     /* at most 50 answers a second */
        uint8_t a[PKT] = {0}; memcpy(a, q, 20); a[5] = 'A';
        char name[17] = {0}; int cls = 0, level = 0, diff = 0, players = 1;
        d2net_host_info(name, sizeof name, &cls, &level, &diff, &players);
        a[20] = (uint8_t)players; a[21] = 8; a[22] = (uint8_t)diff; a[23] = (uint8_t)cls; a[24] = (uint8_t)level; a[25] = (uint8_t)(level >> 8);
        memcpy(a + 26, name, 16);
        sendto(fd, a, sizeof a, 0, (struct sockaddr *)&from, fl);
    }
    return 0;
}
/* the game listens on (on = 1) or closes (0) its TCP game port (winsock.c) */
void d2net_hosting(int on, uint16_t port)
{
    if (port != D2NET_PORT || !list_on()) return;
    void portmap_open_ports(const uint16_t *ports, const int *tcp, int n); void portmap_close(void);
    group_init();
    pthread_mutex_lock(&lock);
    if (on == hosting) { pthread_mutex_unlock(&lock); return; }
    hosting = on;
    if (on) {
        memcpy(host_group, group_hash, 8);
        host_fd = udp_socket(D2NET_PORT);
        if (host_fd >= 0) pthread_create(&host_th, 0, host_thread, 0);
        else fprintf(stderr, "d2net: UDP %d is taken: this game won't answer the game list\n", D2NET_PORT);
    }
    int fd = host_fd; pthread_mutex_unlock(&lock);
    if (on) {
        uint16_t ports[2] = {D2NET_PORT, D2NET_PORT}; int tcp[2] = {1, 0};
        if (router_ok()) portmap_open_ports(ports, tcp, fd >= 0 ? 2 : 1);
        pthread_mutex_lock(&lock); P2pDht *d = p2p(); pthread_mutex_unlock(&lock); if (d) p2p_dht_announce(d, D2NET_PORT);
        fprintf(stderr, "d2net: hosting: %s%s\n", d ? "listed on the DHT game list" : "local network only (D2_NO_P2P)",
                getenv("D2_P2P_GROUP") && *getenv("D2_P2P_GROUP") ? " (private group)" : "");
    } else {
        if (dht) p2p_dht_announce(dht, 0);
        if (router_ok()) portmap_close();
        if (fd >= 0) { pthread_join(host_th, 0); pthread_mutex_lock(&lock); close(host_fd); host_fd = -1; pthread_mutex_unlock(&lock); }
        fprintf(stderr, "d2net: hosting ended\n");
    }
}

/* ---- joining: the list ---- */
static void ask(int fd, uint32_t ip, uint16_t port, uint32_t nonce)
{
    uint8_t q[PKT] = {0}; memcpy(q, MAGIC, 4); q[4] = 1; q[5] = 'Q'; memcpy(q + 8, &nonce, 4); memcpy(q + 12, group_hash, 8);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = ip;
    sendto(fd, q, sizeof q, 0, (struct sockaddr *)&a, sizeof a);
}
static int is_own(uint32_t ip)
{
    struct ifaddrs *ifs = 0; int own = ip == htonl(INADDR_LOOPBACK);
    if (!own && !getifaddrs(&ifs)) {
        for (struct ifaddrs *i = ifs; i && !own; i = i->ifa_next)
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr == ip) own = 1;
        freeifaddrs(ifs);
    }
    return own;
}
static void *search_thread(void *arg)
{
    (void)arg; uint32_t nonce = (uint32_t)arc4random(); double last = 0;
    for (;;) {
        pthread_mutex_lock(&lock); int fd = search_fd, on = searching; pthread_mutex_unlock(&lock);
        if (!on || fd < 0) break;
        double t = now_s();
        if (t - last >= 1) {                                               /* ask everyone, once a second */
            last = t;
            ask(fd, htonl(INADDR_LOOPBACK), D2NET_PORT, nonce);            /* a game hosted on this Mac */
            ask(fd, htonl(INADDR_BROADCAST), D2NET_PORT, nonce);           /* the local network */
            struct ifaddrs *ifs = 0;
            if (!getifaddrs(&ifs)) {
                for (struct ifaddrs *i = ifs; i; i = i->ifa_next)
                    if (i->ifa_broadaddr && i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_BROADCAST) && !(i->ifa_flags & IFF_LOOPBACK))
                        ask(fd, ((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr.s_addr, D2NET_PORT, nonce);
                freeifaddrs(ifs);
            }
            if (dht) { uint32_t ip[64]; uint16_t pt[64]; int n = p2p_dht_peers(dht, ip, pt, 64); for (int k = 0; k < n; k++) ask(fd, ip[k], pt[k], nonce); }
        }
        struct pollfd p = {fd, POLLIN, 0}; if (poll(&p, 1, 200) <= 0) continue;
        uint8_t a[PKT + 16]; struct sockaddr_in from; socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd, a, sizeof a, 0, (struct sockaddr *)&from, &fl);
        if (n != PKT || memcmp(a, MAGIC, 4) || a[4] != 1 || a[5] != 'A' || memcmp(a + 8, &nonce, 4) || memcmp(a + 12, group_hash, 8)) continue;
        D2NetGame g = {0};
        g.ip = is_own(from.sin_addr.s_addr) ? htonl(INADDR_LOOPBACK) : from.sin_addr.s_addr; g.port = D2NET_PORT;
        g.players = a[20] > 8 ? 8 : a[20]; g.max = a[21] > 8 ? 8 : a[21]; g.difficulty = a[22] > 2 ? 0 : a[22]; g.cls = a[23] > 6 ? 0 : a[23];
        g.level = a[24] | a[25] << 8; if (g.level > 99) g.level = 0;
        for (int k = 0; k < 16; k++) { uint8_t c = a[26 + k]; if (!c) break; g.name[k] = c >= 32 && c < 127 ? (char)c : '?'; }
        g.local = (ntohl(g.ip) >> 24) == 127 || (ntohl(g.ip) >> 24) == 10 || (ntohl(g.ip) >> 20) == 0xac1 || (ntohl(g.ip) >> 16) == 0xc0a8;
        g.seen = now_s();
        pthread_mutex_lock(&lock);
        int k = 0; while (k < ngames && games[k].ip != g.ip) k++;
        if (k < MAXGAMES) { games[k] = g; if (k == ngames) ngames++; }
        pthread_mutex_unlock(&lock);
    }
    return 0;
}
/* start (on = 1) or stop looking for games (the Join Game screen) */
void d2net_search(int on)
{
    group_init();
    pthread_mutex_lock(&lock);
    if (on == searching) { pthread_mutex_unlock(&lock); return; }
    searching = on;
    if (on) { search_fd = udp_socket(0); if (search_fd >= 0) pthread_create(&search_th, 0, search_thread, 0); }
    int fd = search_fd; pthread_mutex_unlock(&lock);
    if (on) { pthread_mutex_lock(&lock); P2pDht *d = p2p(); pthread_mutex_unlock(&lock); if (d) p2p_dht_search(d, 20); }
    else if (fd >= 0) { pthread_join(search_th, 0); pthread_mutex_lock(&lock); close(search_fd); search_fd = -1; ngames = 0; pthread_mutex_unlock(&lock); }
}
/* the games that answered in the last 10 seconds (at most cap) */
int d2net_games(D2NetGame *out, int cap)
{
    double t = now_s(); int n = 0;
    pthread_mutex_lock(&lock);
    for (int k = 0; k < ngames && n < cap; k++) if (t - games[k].seen < 10) out[n++] = games[k];
    pthread_mutex_unlock(&lock);
    return n;
}
