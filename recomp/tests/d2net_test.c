/* d2net_test: the game list (runtime/win32/d2net.c) without the game: a host on this Mac is found with its details;
 * a different group neither finds nor gets an answer; malformed and oversized questions get none; the host answers at
 * most 50 questions a second; hosting stops answering when it ends. No DHT (D2_NO_P2P=1) and no router (portmap stubbed).
 * Build: clang -O1 -g -fsanitize=address,undefined -I runtime -I runtime/win32 tests/d2net_test.c runtime/win32/p2p_dht.c -lpthread -o /tmp/d2net_test */
#define D2NET_TEST
#include "../runtime/win32/d2net.c"
void portmap_open_ports(const uint16_t *ports, const int *tcp, int n) { (void)ports; (void)tcp; (void)n; }
void portmap_close(void) {}
void portmap_cleanup(void) {}
uint32_t portmap_public_ip(void) { return 0; }
int d2net_host_info(char *name, int cap, int *cls, int *level, int *difficulty, int *players)
{ snprintf(name, (size_t)cap, "Tester"); *cls = 3; *level = 42; *difficulty = 1; *players = 2; return 1; }
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
static int answers(const uint8_t *q, size_t n, int count)      /* send a raw question `count` times; answers received */
{
    int s = udp_socket(0), got = 0; struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(4000); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int k = 0; k < count; k++) sendto(s, q, n, 0, (struct sockaddr *)&a, sizeof a);
    for (double t = now_s(); now_s() - t < 1.0;) { struct pollfd p = {s, POLLIN, 0}; if (poll(&p, 1, 100) > 0) { uint8_t b[128]; if (recv(s, b, sizeof b, 0) == PKT) got++; } }
    close(s); return got;
}
int main(void)
{
    setenv("D2_NO_P2P", "1", 1); setenv("D2_P2P_GROUP", "friends", 1);
    d2net_hosting(1, 4000); d2net_search(1); usleep(2500000);
    D2NetGame g[8]; int n = d2net_games(g, 8);
    CHECK(n == 1 && g[0].ip == htonl(INADDR_LOOPBACK) && !strcmp(g[0].name, "Tester") && g[0].level == 42 && g[0].cls == 3 && g[0].difficulty == 1 && g[0].players == 2,
          "a game hosted on this Mac is listed with its details");
    d2net_search(0);
    uint8_t q[PKT] = {0}; memcpy(q, MAGIC, 4); q[4] = 1; q[5] = 'Q'; memcpy(q + 12, group_hash, 8);
    CHECK(answers(q, PKT, 1) == 1, "a well-formed question is answered");
    uint8_t big[PKT + 8] = {0}; memcpy(big, q, PKT);
    CHECK(answers(q, PKT - 1, 1) == 0 && answers(big, sizeof big, 1) == 0, "short and oversized questions are not");
    uint8_t w[PKT]; memcpy(w, q, PKT); w[12] ^= 1;
    CHECK(answers(w, PKT, 1) == 0, "a question from another group is not");
    w[12] ^= 1; w[5] = 'A'; CHECK(answers(w, PKT, 1) == 0, "an answer sent to the host is not");
    int many = answers(q, PKT, 300); CHECK(many <= 52, "at most 50 answers a second (a flood of 300)");
    group_ready = 0; setenv("D2_P2P_GROUP", "strangers", 1); group_init(); d2net_search(1); usleep(2000000);
    CHECK(d2net_games(g, 8) == 0, "another group's search finds nothing");
    d2net_search(0); group_ready = 0; setenv("D2_P2P_GROUP", "friends", 1); group_init();
    d2net_hosting(0, 4000); usleep(600000);
    CHECK(answers(q, PKT, 1) == 0, "nothing answers after hosting ends");
    printf(fails ? "FAILED\n" : "all passed\n"); return fails != 0;
}
