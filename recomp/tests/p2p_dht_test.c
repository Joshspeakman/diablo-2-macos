/* p2p_dht_test: the game list's DHT client (runtime/win32/p2p_dht.c) against a small DHT of fake nodes on this computer.
 *   sha1      FIPS 180-1 test vectors
 *   announce  a host announces its port; the nodes nearest the key acknowledge it
 *   find      a player's lookup, starting from one node, finds the host's address and port
 *   stop      a host that stops announcing is not announced again
 *   garbage   nodes that answer with malformed packets, wrong transaction ids or nothing at all do not stop a lookup
 * The fake nodes answer get_peers (their id, a token, the 8 nodes nearest the key that they know, the hosts stored for
 * it) and announce_peer (store the sender's address with the port given, if the token is theirs).
 * Build: clang -O1 -g -I runtime/win32 tests/p2p_dht_test.c runtime/win32/p2p_dht.c -lpthread -o /tmp/p2p_dht_test */
#include "p2p_dht.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { NODES = 20 };
typedef struct { int fd; uint16_t port; uint8_t id[20]; int bad; struct { uint32_t ip; uint16_t port; } stored[8]; int nstored; int announces; } Node;
static Node N[NODES]; static volatile int running = 1; static int asked_bad[NODES]; static uint8_t KEY[20];
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static int fails;
static void check(int ok, const char *what, const char *msg) { printf("%s %-9s %s\n", ok ? "ok  " : "FAIL", what, msg); if (!ok) fails++; }

static const uint8_t *find(const uint8_t *p, size_t n, const char *key, size_t *len)   /* "<len>:key" then a string value */
{
    char pat[64]; int pl = snprintf(pat, sizeof pat, "%zu:%s", strlen(key), key);
    for (size_t i = 0; i + (size_t)pl < n; i++) if (!memcmp(p + i, pat, (size_t)pl)) {
        const uint8_t *q = p + i + pl; size_t l = 0; while (q < p + n && *q >= '0' && *q <= '9') l = l * 10 + (size_t)(*q++ - '0');
        if (q < p + n && *q == ':') { *len = l; return q + 1; }
        if (q < p + n && *q == 'i') { *len = 0; return q; }
        return 0;
    }
    return 0;
}
static int closer(const uint8_t *k, const uint8_t *a, const uint8_t *b) { for (int i = 0; i < 20; i++) { uint8_t x = a[i] ^ k[i], y = b[i] ^ k[i]; if (x != y) return x < y; } return 0; }
static void *node_thread(void *arg)
{
    Node *me = arg; uint8_t buf[2048], out[2048];
    while (running) {
        struct pollfd pf = {me->fd, POLLIN, 0}; if (poll(&pf, 1, 50) <= 0) continue;
        struct sockaddr_in a; socklen_t al = sizeof a; ssize_t n = recvfrom(me->fd, buf, sizeof buf, 0, (struct sockaddr *)&a, &al);
        if (n <= 0) continue;
        size_t tl, ql; const uint8_t *t = find(buf, (size_t)n, "t", &tl), *q = find(buf, (size_t)n, "q", &ql);
        if (!t || !q || tl < 1 || tl > 8) continue;
        size_t o = 0;
        if (me->bad) asked_bad[me - N]++;
        if (me->bad == 1) continue;                                       /* silent */
        if (me->bad == 2) { o = (size_t)snprintf((char *)out, sizeof out, "d1:rd2:id20:"); memcpy(out + o, me->id, 20); o += 20; memcpy(out + o, "5:nodes999:xx", 13); o += 13; sendto(me->fd, out, o, 0, (struct sockaddr *)&a, al); continue; }   /* truncated */
        uint8_t tt[8]; memcpy(tt, t, tl); if (me->bad == 3) tt[0] ^= 0x55;   /* someone else's transaction */
        if (ql == 9 && !memcmp(q, "get_peers", 9)) {
            uint8_t near[NODES]; int nn = 0;
            for (int i = 0; i < NODES; i++) if (&N[i] != me) near[nn++] = (uint8_t)i;
            for (int i = 1; i < nn; i++) { uint8_t x = near[i]; int j = i - 1; while (j >= 0 && closer(KEY, N[x].id, N[near[j]].id)) { near[j + 1] = near[j]; j--; } near[j + 1] = x; }
            if (nn > 8) nn = 8;
            o = (size_t)snprintf((char *)out, sizeof out, "d1:rd2:id20:"); memcpy(out + o, me->id, 20); o += 20;
            o += (size_t)snprintf((char *)out + o, sizeof out - o, "5:nodes%d:", nn * 26);
            for (int i = 0; i < nn; i++) { Node *x = &N[near[i]]; memcpy(out + o, x->id, 20); uint32_t ip = htonl(INADDR_LOOPBACK); memcpy(out + o + 20, &ip, 4); out[o + 24] = (uint8_t)(x->port >> 8); out[o + 25] = (uint8_t)x->port; o += 26; }
            o += (size_t)snprintf((char *)out + o, sizeof out - o, "5:token2:tk");
            pthread_mutex_lock(&lk);
            if (me->nstored) {
                o += (size_t)snprintf((char *)out + o, sizeof out - o, "6:valuesl");
                for (int i = 0; i < me->nstored; i++) { o += (size_t)snprintf((char *)out + o, sizeof out - o, "6:"); memcpy(out + o, &me->stored[i].ip, 4); out[o + 4] = (uint8_t)(me->stored[i].port >> 8); out[o + 5] = (uint8_t)me->stored[i].port; o += 6; }
                out[o++] = 'e';
            }
            pthread_mutex_unlock(&lk);
        } else if (ql == 13 && !memcmp(q, "announce_peer", 13)) {
            size_t kl; const uint8_t *tok = find(buf, (size_t)n, "token", &kl); size_t pl; const uint8_t *pp = find(buf, (size_t)n, "port", &pl);
            if (!tok || kl != 2 || memcmp(tok, "tk", 2) || !pp) continue;
            unsigned port = (unsigned)atoi((const char *)pp + 1);
            pthread_mutex_lock(&lk);
            int k = 0; while (k < me->nstored && !(me->stored[k].ip == a.sin_addr.s_addr && me->stored[k].port == port)) k++;
            if (k == me->nstored && k < 8) { me->stored[k].ip = a.sin_addr.s_addr; me->stored[k].port = (uint16_t)port; me->nstored++; }
            me->announces++;
            pthread_mutex_unlock(&lk);
            o = (size_t)snprintf((char *)out, sizeof out, "d1:rd2:id20:"); memcpy(out + o, me->id, 20); o += 20;
        } else continue;
        o += (size_t)snprintf((char *)out + o, sizeof out - o, "e1:t%zu:", tl); memcpy(out + o, tt, tl); o += tl;
        o += (size_t)snprintf((char *)out + o, sizeof out - o, "1:y1:re");
        sendto(me->fd, out, o, 0, (struct sockaddr *)&a, al);
    }
    return 0;
}
static int wait_for(int (*cond)(void *), void *arg, double seconds)
{
    struct timespec t0, t; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) { if (cond(arg)) return 1; clock_gettime(CLOCK_MONOTONIC, &t); if ((t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) * 1e-9 > seconds) return 0; usleep(20000); }
}
static int announced(void *d) { int a; p2p_dht_stats(d, 0, 0, &a); return a >= 4; }
static int found(void *d)
{
    uint32_t ip[16]; uint16_t port[16]; int n = p2p_dht_peers(d, ip, port, 16);
    for (int i = 0; i < n; i++) if (ip[i] == htonl(INADDR_LOOPBACK) && port[i] == 3568) return 1;
    return 0;
}
static int total_announces(void) { int s = 0; pthread_mutex_lock(&lk); for (int i = 0; i < NODES; i++) s += N[i].announces; pthread_mutex_unlock(&lk); return s; }
int main(void)
{
    {   uint8_t h[20]; char hex[41];
        p2p_sha1("abc", 3, h); for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
        check(!strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"), "sha1", hex);
        const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; p2p_sha1(s, (unsigned)strlen(s), h);
        for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
        check(!strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), "sha1", hex); }
    p2p_key("p2p_dht_test", KEY);
    pthread_t th[NODES]; srand(7);
    for (int i = 0; i < NODES; i++) {
        N[i].fd = socket(AF_INET, SOCK_DGRAM, 0); struct sockaddr_in a = {0};
#ifdef __APPLE__
        a.sin_len = sizeof a;
#endif
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(N[i].fd, (struct sockaddr *)&a, sizeof a); socklen_t al = sizeof a; getsockname(N[i].fd, (struct sockaddr *)&a, &al);
        N[i].port = ntohs(a.sin_port); for (int k = 0; k < 20; k++) N[i].id[k] = (uint8_t)rand();
        N[i].bad = i == 3 ? 1 : i == 5 ? 2 : i == 7 ? 3 : 0;       /* a silent node, a garbage node, a wrong-transaction node */
        if (N[i].bad) { memcpy(N[i].id, KEY, 20); N[i].id[19] ^= (uint8_t)i; }   /* nearest the key: asked first */
        pthread_create(&th[i], 0, node_thread, &N[i]);
    }
    char boot[64]; snprintf(boot, sizeof boot, "127.0.0.1:%u", N[0].port);
    P2pDht *host = p2p_dht_start(KEY, boot), *player = p2p_dht_start(KEY, boot);
    p2p_dht_announce(host, 3568);
    int ok = wait_for(announced, host, 10); int nodes, lookups, ann; p2p_dht_stats(host, &nodes, &lookups, &ann);
    char msg[200]; snprintf(msg, sizeof msg, "%d announces acknowledged, %d nodes answered, %d lookups", ann, nodes, lookups);
    check(ok, "announce", msg);
    p2p_dht_search(player, 1);
    ok = wait_for(found, player, 10); p2p_dht_stats(player, &nodes, &lookups, 0);
    snprintf(msg, sizeof msg, "host 127.0.0.1:3568 %s; %d nodes answered, %d lookups", ok ? "found" : "not found", nodes, lookups);
    check(ok, "find", msg);
    int asked = 0; for (int i = 0; i < NODES; i++) if (N[i].bad && asked_bad[i]) asked++;
    snprintf(msg, sizeof msg, "%d of the 3 nodes nearest the key (silent, truncated answer, foreign transaction id) asked and passed over", asked);
    check(ok && asked == 3, "garbage", msg);
    p2p_dht_announce(host, 0); usleep(300000); int before = total_announces(); sleep(2);
    snprintf(msg, sizeof msg, "%d announces after stopping", total_announces() - before);
    check(total_announces() == before, "stop", msg);
    p2p_dht_stop(host); p2p_dht_stop(player);
    running = 0; for (int i = 0; i < NODES; i++) { pthread_join(th[i], 0); close(N[i].fd); }
    printf(fails ? "%d FAILED\n" : "all passed%.0d\n", fails);
    return fails != 0;
}
