/* A game list without a server: hosts announce themselves on BitTorrent's public DHT (the Mainline DHT, BEP 5) under
 * a key of the game's own, and players look that key up. Only the client half of the protocol: this node asks other
 * nodes (get_peers, announce_peer) and answers none (it says so with "ro", BEP 43). IPv4. Self-contained, so it is
 * tested on its own (tests/p2p_dht_test.c). */
#ifndef P2P_DHT_H
#define P2P_DHT_H
#include <stdint.h>
typedef struct P2pDht P2pDht;
/* starts the node's thread. key: the game's 20-byte key (p2p_key). bootstrap: "host:port,host:port" or NULL for the
 * public routers (P2P_DHT_BOOTSTRAP overrides it). */
P2pDht *p2p_dht_start(const uint8_t key[20], const char *bootstrap);
/* announce `port` (UDP, the game's) under the key while > 0; 0 stops announcing */
void p2p_dht_announce(P2pDht *d, uint16_t port);
/* look the key up now and every `every_s` seconds after (0: once) */
void p2p_dht_search(P2pDht *d, int every_s);
/* the hosts found so far (newest first, at most cap): ip in network order, port in host order; returns the count */
int p2p_dht_peers(P2pDht *d, uint32_t *ip, uint16_t *port, int cap);
/* counters for logs and tests: nodes that answered, lookups finished, announces acknowledged */
void p2p_dht_stats(P2pDht *d, int *nodes, int *lookups, int *announced);
void p2p_dht_stop(P2pDht *d);
/* this node's public address as the answering DHT nodes report it (BEP 42), once two agree; 0 before (network order) */
uint32_t p2p_dht_external_ip(P2pDht *d);
/* the game's key: SHA-1 of a name */
void p2p_key(const char *name, uint8_t out[20]);
void p2p_sha1(const void *data, unsigned len, uint8_t out[20]);
#endif
