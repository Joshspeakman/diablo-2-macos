/* The DHT client behind the game list (p2p_dht.h). A lookup is BEP 5's iterative get_peers: start from the nodes
 * known closest to the key (at first the public routers), ask the closest few not yet asked, add the nodes each answer
 * names, and stop when the K closest that answered have all been asked. Answers carry the hosts announced under the
 * key ("values") and a token; a host then announces its port to those K nodes with their tokens. Announcements live
 * about 30 minutes on the nodes, so a host announces again every 10; a player's list keeps hosts seen in the last 15. */
#include "p2p_dht.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { K = 8, MAXC = 160, MAXGOOD = 256, MAXPEERS = 512, MAXANN = 16, INFLIGHT = 6 };
#define QUERY_TIMEOUT 2.0
#define LOOKUP_LIMIT 30.0
#define REANNOUNCE 600.0
#define PEER_KEEP 900.0
static const char *ROUTERS = "router.bittorrent.com:6881,dht.transmissionbt.com:6881,router.utorrent.com:6881,dht.libtorrent.org:25401";

enum { C_NEW, C_SENT, C_DONE, C_FAIL };
typedef struct { uint8_t id[20]; int has_id; uint32_t ip; uint16_t port; int state; double sent; uint32_t tid; uint8_t token[64]; int toklen; } Cand;
struct P2pDht {
    uint8_t key[20], self[20];
    int fd; pthread_t th; pthread_mutex_t m; int stop;
    char boot[512];
    uint16_t ann_port; int search_every, want; double next_lookup, next_announce;
    int active; Cand c[MAXC]; int nc; double started;
    struct { uint8_t id[20]; uint32_t ip; uint16_t port; } good[MAXGOOD]; int ngood;
    struct { uint32_t ip; uint16_t port; double seen; } peers[MAXPEERS]; int npeers;
    uint32_t ann_tid[MAXANN]; int nann;
    int nodes_ok, lookups, announced; uint64_t rng; int local_ok;
    struct { uint32_t ip; int votes; } ext[8]; int next; uint32_t ext_ip;   /* this node's address as answering nodes see it (BEP 42 "ip") */
};

static int dlog = -1;                                      /* P2P_DHT_LOG=1: each lookup's progress on stderr */
#define LOG(...) do { if (dlog < 0) dlog = getenv("P2P_DHT_LOG") != 0; if (dlog) fprintf(stderr, "dht: " __VA_ARGS__); } while (0)
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static int closer(const uint8_t *key, const uint8_t *a, const uint8_t *b)   /* a nearer the key than b (XOR metric) */
{
    for (int i = 0; i < 20; i++) { uint8_t x = a[i] ^ key[i], y = b[i] ^ key[i]; if (x != y) return x < y; }
    return 0;
}
static int cand_before(const P2pDht *d, const Cand *a, const Cand *b)  /* nodes with no id yet (the routers) go last */
{
    if (a->has_id != b->has_id) return a->has_id;
    return a->has_id && closer(d->key, a->id, b->id);
}
static void cand_add(P2pDht *d, const uint8_t *id, uint32_t ip, uint16_t port)
{
    if (!ip || !port) return;
    for (int i = 0; i < d->nc; i++) if (d->c[i].ip == ip && d->c[i].port == port) return;
    Cand n = {{0}, id != 0, ip, port, C_NEW, 0, 0, {0}, 0}; if (id) memcpy(n.id, id, 20);
    if (d->nc == MAXC) {                                   /* full: only a nearer node replaces the farthest */
        if (!cand_before(d, &n, &d->c[MAXC - 1])) return;
        d->nc--;
    }
    int at = d->nc; while (at > 0 && cand_before(d, &n, &d->c[at - 1])) at--;
    memmove(&d->c[at + 1], &d->c[at], sizeof(Cand) * (size_t)(d->nc - at));
    d->c[at] = n; d->nc++;
}
static void good_add(P2pDht *d, const uint8_t *id, uint32_t ip, uint16_t port)
{
    for (int i = 0; i < d->ngood; i++) if (d->good[i].ip == ip && d->good[i].port == port) { memcpy(d->good[i].id, id, 20); return; }
    int at = d->ngood < MAXGOOD ? d->ngood++ : rand() % MAXGOOD;
    memcpy(d->good[at].id, id, 20); d->good[at].ip = ip; d->good[at].port = port;
}
static void peer_add(P2pDht *d, uint32_t ip, uint16_t port)
{
    if (!ip || !port) return;
    double t = now_s();
    for (int i = 0; i < d->npeers; i++) if (d->peers[i].ip == ip && d->peers[i].port == port) { d->peers[i].seen = t; return; }
    int at = d->npeers < MAXPEERS ? d->npeers++ : 0;
    if (at == 0 && d->npeers == MAXPEERS) for (int i = 1; i < MAXPEERS; i++) if (d->peers[i].seen < d->peers[at].seen) at = i;
    d->peers[at].ip = ip; d->peers[at].port = port; d->peers[at].seen = t;
}

/* ---- bencoding: just what KRPC needs ---- */
static size_t put(uint8_t *o, size_t n, const char *s, size_t l) { memcpy(o + n, s, l); return n + l; }
static size_t put_str(uint8_t *o, size_t n, const void *s, size_t l) { char h[16]; int k = snprintf(h, sizeof h, "%zu:", l); n = put(o, n, h, (size_t)k); return put(o, n, s, l); }
static const uint8_t *b_skip(const uint8_t *p, const uint8_t *e, int depth)   /* the end of the value at p, or NULL */
{
    if (p >= e || depth > 32) return 0;
    if (*p == 'i') { p++; while (p < e && *p != 'e') p++; return p < e ? p + 1 : 0; }
    if (*p == 'l' || *p == 'd') { p++; while (p < e && *p != 'e') { p = b_skip(p, e, depth + 1); if (!p) return 0; } return p < e ? p + 1 : 0; }
    if (*p >= '0' && *p <= '9') {
        size_t l = 0; while (p < e && *p >= '0' && *p <= '9') { l = l * 10 + (size_t)(*p - '0'); if (l > 1 << 20) return 0; p++; }
        if (p >= e || *p != ':' || (size_t)(e - p - 1) < l) return 0;
        return p + 1 + l;
    }
    return 0;
}
static int b_str(const uint8_t *p, const uint8_t *e, const uint8_t **s, size_t *l)
{
    if (p >= e || *p < '0' || *p > '9') return 0;
    size_t n = 0; while (p < e && *p >= '0' && *p <= '9') { n = n * 10 + (size_t)(*p - '0'); if (n > 1 << 20) return 0; p++; }
    if (p >= e || *p != ':' || (size_t)(e - p - 1) < n) return 0;
    *s = p + 1; *l = n; return 1;
}
/* in the dict at p, the value of `key`: [*v, *ve) */
static int b_get(const uint8_t *p, const uint8_t *e, const char *key, const uint8_t **v, const uint8_t **ve)
{
    if (p >= e || *p != 'd') return 0;
    p++; size_t kl = strlen(key);
    while (p < e && *p != 'e') {
        const uint8_t *ks; size_t l; if (!b_str(p, e, &ks, &l)) return 0;
        const uint8_t *val = ks + l, *end = b_skip(val, e, 0); if (!end) return 0;
        if (l == kl && !memcmp(ks, key, l)) { *v = val; *ve = end; return 1; }
        p = end;
    }
    return 0;
}

/* ---- queries ---- */
static void send_to(P2pDht *d, uint32_t ip, uint16_t port, const uint8_t *msg, size_t n)
{
    struct sockaddr_in a; memset(&a, 0, sizeof a);
#ifdef __APPLE__
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET; a.sin_addr.s_addr = ip; a.sin_port = htons(port);
    sendto(d->fd, msg, n, 0, (struct sockaddr *)&a, sizeof a);
}
/* transaction ids: 32 random bits (xorshift seeded from the system), so an answer can't be forged by someone who doesn't
 * see the query */
static uint32_t new_tid(P2pDht *d)
{
    uint64_t x = d->rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; d->rng = x;
    return (uint32_t)(x >> 16) | 1;
}
/* Addresses taken from other nodes: only public ones, unless the bootstrap nodes are themselves on this computer or a
 * private network (tests, a LAN). Otherwise a node could have this one send to machines on the player's own network. */
static int addr_ok(const P2pDht *d, uint32_t ip, uint16_t port)
{
    uint32_t h = ntohl(ip);
    if (!ip || port < 1024) return 0;
    if (d->local_ok) return 1;
    return !((h >> 24) == 0 || (h >> 24) == 10 || (h >> 24) == 127 || (h >> 20) == 0xac1 || (h >> 16) == 0xc0a8 ||
             (h >> 22) == (100 << 2 | 1) || (h >> 16) == 0xa9fe || (h >> 28) >= 0xe ||
             (h >> 8) == 0xc00000 || (h >> 8) == 0xc00002 || (h >> 8) == 0xc63364 || (h >> 8) == 0xcb0071 || (h >> 17) == (198 << 7 | 9));
}
static void query_get_peers(P2pDht *d, Cand *c)
{
    uint8_t m[256]; size_t n = 0; uint8_t t[4];
    c->tid = new_tid(d); for (int k = 0; k < 4; k++) t[k] = (uint8_t)(c->tid >> (24 - 8 * k));
    n = put(m, n, "d1:ad2:id", 9); n = put_str(m, n, d->self, 20); n = put(m, n, "9:info_hash", 11); n = put_str(m, n, d->key, 20);
    n = put(m, n, "e1:q9:get_peers2:roi1e1:t", 25); n = put_str(m, n, t, 4); n = put(m, n, "1:y1:qe", 7);
    send_to(d, c->ip, c->port, m, n);
    c->state = C_SENT; c->sent = now_s();
}
static void query_announce(P2pDht *d, Cand *c)
{
    uint8_t m[400]; size_t n = 0; uint8_t t[4]; char pi[32];
    uint32_t tid = new_tid(d); for (int k = 0; k < 4; k++) t[k] = (uint8_t)(tid >> (24 - 8 * k));
    n = put(m, n, "d1:ad2:id", 9); n = put_str(m, n, d->self, 20); n = put(m, n, "9:info_hash", 11); n = put_str(m, n, d->key, 20);
    int k = snprintf(pi, sizeof pi, "4:porti%ue", d->ann_port); n = put(m, n, pi, (size_t)k);
    n = put(m, n, "5:token", 7); n = put_str(m, n, c->token, (size_t)c->toklen);
    n = put(m, n, "e1:q13:announce_peer2:roi1e1:t", 30); n = put_str(m, n, t, 4); n = put(m, n, "1:y1:qe", 7);
    send_to(d, c->ip, c->port, m, n);
    d->ann_tid[d->nann++ % MAXANN] = tid;
}

static void lookup_start(P2pDht *d)
{
    d->nc = 0; d->active = 1; d->started = now_s(); d->want = 0;
    for (int i = 0; i < d->ngood; i++) cand_add(d, d->good[i].id, d->good[i].ip, d->good[i].port);
    char list[512]; snprintf(list, sizeof list, "%s", d->boot);
    pthread_mutex_unlock(&d->m);                           /* name lookups can take a while */
    uint32_t ips[16]; uint16_t ports[16]; int nb = 0;
    for (char *save, *tok = strtok_r(list, ",", &save); tok && nb < 16; tok = strtok_r(0, ",", &save)) {
        char *colon = strrchr(tok, ':'); if (!colon) continue;
        *colon = 0; struct addrinfo h = {0}, *r = 0; h.ai_family = AF_INET; h.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(tok, colon + 1, &h, &r) || !r) continue;
        ips[nb] = ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr; ports[nb] = ntohs(((struct sockaddr_in *)r->ai_addr)->sin_port); nb++;
        freeaddrinfo(r);
    }
    pthread_mutex_lock(&d->m);
    for (int i = 0; i < nb; i++) cand_add(d, 0, ips[i], ports[i]);
}
static void lookup_step(P2pDht *d)
{
    double t = now_s(); int inflight = 0;
    for (int i = 0; i < d->nc; i++) {
        if (d->c[i].state == C_SENT && t - d->c[i].sent > QUERY_TIMEOUT) d->c[i].state = C_FAIL;
        if (d->c[i].state == C_SENT) inflight++;
    }
    for (int i = 0; i < d->nc && inflight < INFLIGHT; i++) if (d->c[i].state == C_NEW) { query_get_peers(d, &d->c[i]); inflight++; }
    /* done when the K closest still in the running have all answered (or nothing is left to ask, or time is up) */
    int seen = 0, waiting = 0, pending = 0;
    for (int i = 0; i < d->nc && seen < K; i++) {
        if (d->c[i].state == C_FAIL) continue;
        seen++; if (d->c[i].state != C_DONE) waiting = 1;
    }
    for (int i = 0; i < d->nc; i++) if (d->c[i].state == C_NEW || d->c[i].state == C_SENT) pending = 1;
    if ((seen && !waiting) || !pending || t - d->started > LOOKUP_LIMIT) {
        if (d->ann_port) {
            int sent = 0;
            for (int i = 0; i < d->nc && sent < K; i++) if (d->c[i].state == C_DONE && d->c[i].toklen) { query_announce(d, &d->c[i]); sent++; }
            d->next_announce = t + REANNOUNCE;
        }
        { int done = 0, failed = 0; for (int i = 0; i < d->nc; i++) { done += d->c[i].state == C_DONE; failed += d->c[i].state == C_FAIL; }
          LOG("lookup %d finished after %.1f s: %d candidates, %d answered, %d failed, %d hosts known%s\n", d->lookups + 1, t - d->started, d->nc, done, failed, d->npeers, d->ann_port ? ", announcing" : ""); }
        d->active = 0; d->lookups++;
        d->next_lookup = d->search_every ? t + d->search_every : 0;
    }
}
static void on_packet(P2pDht *d, const uint8_t *p, size_t n, uint32_t ip, uint16_t port)
{
    const uint8_t *e = p + n, *v, *ve, *s; size_t l;
    if (!b_get(p, e, "y", &v, &ve) || !b_str(v, ve, &s, &l) || l != 1 || s[0] != 'r') { LOG("not an answer (%zu bytes, y=%c)\n", n, l == 1 ? s[0] : '?'); return; }   /* answers only */
    if (!b_get(p, e, "t", &v, &ve) || !b_str(v, ve, &s, &l) || l != 4) return;
    uint32_t tid = (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
    for (int i = 0; i < MAXANN; i++) if (d->ann_tid[i] == tid) { d->ann_tid[i] = 0; d->announced++; return; }
    const uint8_t *r, *re; if (!b_get(p, e, "r", &r, &re)) return;
    Cand *c = 0; for (int i = 0; i < d->nc; i++) if (d->c[i].tid == tid && d->c[i].state != C_DONE && d->c[i].ip == ip && d->c[i].port == port) { c = &d->c[i]; break; }
    if (!c) return;
    uint8_t id[20] = {0}; int has = 0;
    if (b_get(r, re, "id", &v, &ve) && b_str(v, ve, &s, &l) && l == 20) { memcpy(id, s, 20); has = 1; }
    c->state = C_DONE; d->nodes_ok++;
    if (b_get(p, e, "ip", &v, &ve) && b_str(v, ve, &s, &l) && l == 6) {   /* the address this node sees us at: two nodes agreeing make it */
        uint32_t me; memcpy(&me, s, 4); int k = 0;
        while (k < d->next && d->ext[k].ip != me) k++;
        if (k == d->next && k < 8) { d->ext[k].ip = me; d->ext[k].votes = 0; d->next++; }
        if (k < 8 && ++d->ext[k].votes >= 2 && addr_ok(d, me, 1) && d->ext_ip != me) {
            d->ext_ip = me; char a[32]; inet_ntop(AF_INET, &me, a, sizeof a); LOG("this node's public address: %s\n", a);
        }
    }
    if (b_get(r, re, "token", &v, &ve) && b_str(v, ve, &s, &l) && l <= sizeof c->token) { memcpy(c->token, s, l); c->toklen = (int)l; }
    if (b_get(r, re, "values", &v, &ve) && *v == 'l') {
        const uint8_t *q = v + 1; int taken = 0;
        while (q < ve && *q != 'e' && taken < 100) {      /* at most 100 hosts from one answer */
            if (!b_str(q, ve, &s, &l)) break;
            if (l == 6) { uint32_t pip; memcpy(&pip, s, 4); uint16_t pp = (uint16_t)(s[4] << 8 | s[5]); if (addr_ok(d, pip, pp)) { peer_add(d, pip, pp); taken++; } }
            q = s + l;
        }
    }
    if (has) {
        good_add(d, id, ip, port);
        if (!c->has_id) { memcpy(c->id, id, 20); c->has_id = 1; }
    }
    if (b_get(r, re, "nodes", &v, &ve) && b_str(v, ve, &s, &l)) {
        for (size_t o = 0; o + 26 <= l && o < 26 * 16; o += 26) {   /* at most 16 nodes from one answer */
            uint32_t nip; memcpy(&nip, s + o + 20, 4); uint16_t np = (uint16_t)(s[o + 24] << 8 | s[o + 25]);
            if (addr_ok(d, nip, np)) cand_add(d, s + o, nip, np);
        }
    }
}
static void *dht_thread(void *arg)
{
    P2pDht *d = arg; uint8_t buf[2048];
    pthread_mutex_lock(&d->m);
    while (!d->stop) {
        double t = now_s();
        if (!d->active && (d->want || (d->ann_port && t >= d->next_announce) || (d->next_lookup && t >= d->next_lookup))) lookup_start(d);
        if (d->active) lookup_step(d);
        pthread_mutex_unlock(&d->m);
        struct pollfd pf = {d->fd, POLLIN, 0};
        if (poll(&pf, 1, 100) > 0) {
            struct sockaddr_in a; socklen_t al = sizeof a;
            ssize_t n;
            while ((n = recvfrom(d->fd, buf, sizeof buf, 0, (struct sockaddr *)&a, &al)) > 0) {
                pthread_mutex_lock(&d->m); on_packet(d, buf, (size_t)n, a.sin_addr.s_addr, ntohs(a.sin_port)); pthread_mutex_unlock(&d->m);
                al = sizeof a;
            }
        }
        pthread_mutex_lock(&d->m);
    }
    pthread_mutex_unlock(&d->m);
    return 0;
}

P2pDht *p2p_dht_start(const uint8_t key[20], const char *bootstrap)
{
    P2pDht *d = calloc(1, sizeof *d); if (!d) return 0;
    memcpy(d->key, key, 20);
    { int fd = open("/dev/urandom", O_RDONLY); if (fd < 0 || read(fd, d->self, 20) != 20) for (int i = 0; i < 20; i++) d->self[i] = (uint8_t)rand(); if (fd >= 0) close(fd); }
    const char *b = getenv("P2P_DHT_BOOTSTRAP"); if (!b || !*b) b = bootstrap; if (!b || !*b) b = ROUTERS;
    snprintf(d->boot, sizeof d->boot, "%s", b);
    /* private addresses are taken from the nodes only when the bootstrap nodes are given as private addresses */
    d->local_ok = b != ROUTERS && (!strncmp(b, "127.", 4) || !strncmp(b, "10.", 3) || !strncmp(b, "192.168.", 8) || !strncmp(b, "localhost", 9));
    memcpy(&d->rng, d->self, 8); if (!d->rng) d->rng = 0x9e3779b97f4a7c15ull;
    d->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (d->fd < 0) { free(d); return 0; }
    fcntl(d->fd, F_SETFL, fcntl(d->fd, F_GETFL) | O_NONBLOCK); fcntl(d->fd, F_SETFD, FD_CLOEXEC);
    pthread_mutex_init(&d->m, 0);
    if (pthread_create(&d->th, 0, dht_thread, d)) { close(d->fd); free(d); return 0; }
    return d;
}
void p2p_dht_announce(P2pDht *d, uint16_t port)
{
    pthread_mutex_lock(&d->m);
    if (port && port != d->ann_port) d->next_announce = 0;   /* now */
    d->ann_port = port; pthread_mutex_unlock(&d->m);
}
void p2p_dht_search(P2pDht *d, int every_s)
{
    pthread_mutex_lock(&d->m); d->want = 1; d->search_every = every_s; pthread_mutex_unlock(&d->m);
}
int p2p_dht_peers(P2pDht *d, uint32_t *ip, uint16_t *port, int cap)
{
    pthread_mutex_lock(&d->m);
    double t = now_s(); int n = 0;
    int order[MAXPEERS]; for (int i = 0; i < d->npeers; i++) order[i] = i;
    for (int i = 1; i < d->npeers; i++) { int x = order[i], j = i - 1; while (j >= 0 && d->peers[order[j]].seen < d->peers[x].seen) { order[j + 1] = order[j]; j--; } order[j + 1] = x; }
    for (int i = 0; i < d->npeers && n < cap; i++) {
        int k = order[i]; if (t - d->peers[k].seen > PEER_KEEP) continue;
        ip[n] = d->peers[k].ip; port[n] = d->peers[k].port; n++;
    }
    pthread_mutex_unlock(&d->m);
    return n;
}
void p2p_dht_stats(P2pDht *d, int *nodes, int *lookups, int *announced)
{
    pthread_mutex_lock(&d->m);
    if (nodes) *nodes = d->nodes_ok; if (lookups) *lookups = d->lookups; if (announced) *announced = d->announced;
    pthread_mutex_unlock(&d->m);
}
void p2p_dht_stop(P2pDht *d)
{
    if (!d) return;
    pthread_mutex_lock(&d->m); d->stop = 1; pthread_mutex_unlock(&d->m);
    pthread_join(d->th, 0); close(d->fd); pthread_mutex_destroy(&d->m); free(d);
}

/* ---- SHA-1 (FIPS 180-1), for the key ---- */
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; } else { f = b ^ c ^ d; k = 0xca62c1d6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i]; e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
void p2p_sha1(const void *data, unsigned len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    const uint8_t *p = data; unsigned n = len; uint8_t blk[64];
    while (n >= 64) { sha1_block(h, p); p += 64; n -= 64; }
    memset(blk, 0, 64); memcpy(blk, p, n); blk[n] = 0x80;
    if (n >= 56) { sha1_block(h, blk); memset(blk, 0, 64); }
    uint64_t bits = (uint64_t)len * 8; for (int i = 0; i < 8; i++) blk[63 - i] = (uint8_t)(bits >> (8 * i));
    sha1_block(h, blk);
    for (int i = 0; i < 5; i++) { out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16); out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i]; }
}
void p2p_key(const char *name, uint8_t out[20]) { p2p_sha1(name, (unsigned)strlen(name), out); }
uint32_t p2p_dht_external_ip(P2pDht *d)                    /* network order; 0 until two answering nodes agreed on it */
{
    if (!d) return 0;
    pthread_mutex_lock(&d->m); uint32_t r = d->ext_ip; pthread_mutex_unlock(&d->m); return r;
}
