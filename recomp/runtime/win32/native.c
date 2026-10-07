/* Hot routines done natively instead of in lifted code (tools/lift.py HOOKS/OVERRIDES name their addresses). None
 * yet: added where profiling shows a routine is worth it, each checked bit for bit against the lifted original. */
#include "w32.h"

int native_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
int native_hook(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
