/* p2p_dht_fuzz: the DHT client's packet parsing (runtime/win32/p2p_dht.c on_packet, the bencoding readers) fed mutated
 * answers: bit flips, bytes inserted and removed, truncation, length fields made huge or wrong, nesting deepened. Every
 * DHT node on the internet can send this client whatever it likes, so it must not read or write out of bounds (build
 * with -fsanitize=address,undefined) and must keep its own limits (lists, candidates, hosts, private addresses).
 * Deterministic (fixed seed); N iterations (default 2000000, a few seconds).
 * Build: clang -O1 -g -fsanitize=address,undefined -I runtime/win32 tests/p2p_dht_fuzz.c -lpthread -o /tmp/p2p_dht_fuzz */
#include "../runtime/win32/p2p_dht.c"

static uint64_t rs = 0x2545f4914f6cdd1dull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static size_t seed_answer(uint8_t *o, uint32_t tid, int kind)
{
    size_t n = 0; uint8_t t[4] = {(uint8_t)(tid >> 24), (uint8_t)(tid >> 16), (uint8_t)(tid >> 8), (uint8_t)tid}, id[20];
    for (int k = 0; k < 20; k++) id[k] = (uint8_t)rnd();
    n = put(o, n, "d1:rd2:id", 9); n = put_str(o, n, id, 20);
    if (kind & 1) {                                       /* nodes: 4 compact entries, public and private addresses */
        uint8_t nodes[26 * 4];
        for (int k = 0; k < 26 * 4; k++) nodes[k] = (uint8_t)rnd();
        nodes[20] = 10; nodes[46] = 192; nodes[47] = 168; nodes[72] = 127;
        n = put(o, n, "5:nodes", 7); n = put_str(o, n, nodes, sizeof nodes);
    }
    if (kind & 2) { n = put(o, n, "5:token", 7); n = put_str(o, n, "abcdefgh", 8); }
    if (kind & 4) {                                       /* values: hosts */
        n = put(o, n, "6:valuesl", 9);
        for (int k = 0; k < 5; k++) { uint8_t v[6]; for (int j = 0; j < 6; j++) v[j] = (uint8_t)rnd(); n = put_str(o, n, v, 6); }
        o[n++] = 'e';
    }
    n = put(o, n, "e1:t", 4); n = put_str(o, n, t, 4); n = put(o, n, "1:y1:re", 7);
    return n;
}
static size_t mutate(uint8_t *b, size_t n, size_t cap)
{
    int ops = 1 + (int)(rnd() % 6);
    for (int k = 0; k < ops; k++) {
        switch (rnd() % 8) {
        case 0: if (n) b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8)); break;               /* bit flip */
        case 1: if (n) b[rnd() % n] = (uint8_t)rnd(); break;                                /* byte */
        case 2: if (n < cap) { size_t at = rnd() % (n + 1); memmove(b + at + 1, b + at, n - at); b[at] = (uint8_t)rnd(); n++; } break;
        case 3: if (n) { size_t at = rnd() % n; memmove(b + at, b + at + 1, n - at - 1); n--; } break;
        case 4: n = rnd() % (n + 1); break;                                                  /* truncate */
        case 5: for (size_t i = 0; i < n; i++) if (b[i] >= '1' && b[i] <= '9' && rnd() % 4 == 0) { b[i] = '9'; break; }   /* a length grows */
            break;
        case 6: if (n + 8 < cap) { size_t at = rnd() % (n + 1); memmove(b + at + 8, b + at, n - at); memcpy(b + at, "ld1:xli1e", 8); n += 8; } break;   /* nesting */
        case 7: if (n + 12 < cap) { size_t at = rnd() % (n + 1); memmove(b + at + 12, b + at, n - at); memcpy(b + at, "999999999999", 12); n += 12; } break;
        }
    }
    return n;
}
int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 2000000;
    P2pDht *d = calloc(1, sizeof *d); pthread_mutex_init(&d->m, 0); d->rng = 12345;
    uint8_t buf[4096]; long acked = 0;
    for (long it = 0; it < iters; it++) {
        if (it % 1000 == 0) { d->nc = 0; d->npeers = 0; d->ngood = 0; d->local_ok = (int)(it / 1000 % 2); }
        uint32_t tid = new_tid(d); uint32_t ip = htonl(0x08080808u + (uint32_t)(rnd() % 16)); uint16_t port = (uint16_t)(1024 + rnd() % 60000);
        if (d->nc < MAXC) { Cand c = {{0}, 1, ip, port, C_SENT, now_s(), tid, {0}, 0}; for (int k = 0; k < 20; k++) c.id[k] = (uint8_t)rnd(); d->c[d->nc++] = c; }
        size_t n = seed_answer(buf, tid, (int)(rnd() % 8));
        if (rnd() % 8) n = mutate(buf, n, sizeof buf - 16);
        uint8_t *copy = malloc(n ? n : 1); memcpy(copy, buf, n);         /* exactly n bytes: ASan sees any overread */
        int before = d->nodes_ok; on_packet(d, copy, n, ip, port); acked += d->nodes_ok > before; free(copy);
        if (d->nc > MAXC || d->npeers > MAXPEERS || d->ngood > MAXGOOD) { printf("FAIL limits: %d candidates, %d hosts, %d nodes\n", d->nc, d->npeers, d->ngood); return 1; }
        if (!d->local_ok) for (int k = 0; k < d->npeers; k++) if (!addr_ok(d, d->peers[k].ip, d->peers[k].port)) { printf("FAIL a private or reserved host was kept\n"); return 1; }
        if (!d->local_ok) for (int k = 0; k < d->nc; k++) if (d->c[k].ip != ip && !addr_ok(d, d->c[k].ip, d->c[k].port) && d->c[k].state == C_NEW) { printf("FAIL a private node would be asked\n"); return 1; }
    }
    printf("ok   fuzz      %ld answers parsed (%ld accepted), limits held, no private address kept\n", iters, acked);
    return 0;
}
