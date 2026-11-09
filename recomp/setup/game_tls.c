/* Compiled with the game code on the player's Mac. The runtime's thread-local that the recompiled code uses directly lives
 * here: the linker can't bind a thread-local across the split between the ready-made runtime and the game. */
#include <stdint.h>
__thread uint32_t rt_host_rc;
