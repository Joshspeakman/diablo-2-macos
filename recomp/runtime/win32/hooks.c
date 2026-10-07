/* Native features and fixes added to the recompiled game at fixed x86 addresses. tools/lift.py lists them:
 *   HOOKS:     run before the instruction at the address (guest registers spilled to the context), then the original
 *              continues;
 *   OVERRIDES: at a function's first instruction; return 1 when the whole call (including the return) was done here,
 *              0 to run the original.
 * DS_TRACE_HOOKS=<addr,...> at build time adds trace hooks that log registers and stack arguments (DS_HOOKTRACE=1). */
#include "w32.h"
#include "ext.h"

int native_hook(Ctx *c, uint32_t addr);   /* native.c */
void rt_hook(Ctx *c, uint32_t addr)
{
    if (native_hook(c, addr)) return;
    switch (addr) {
    default:   /* development trace hooks (tools/lift.py DS_TRACE_HOOKS) */
        if (getenv("DS_HOOKTRACE")) fprintf(stderr, "hook %08x: eax %08x ecx %08x edx %08x ebx %08x esi %08x edi %08x | ret %08x args %08x %08x %08x %08x\n",
                                            addr, c->eax, c->ecx, c->edx, c->ebx, c->esi, c->edi, rt_r32(G_MEM, c->esp), rt_r32(G_MEM, c->esp + 4),
                                            rt_r32(G_MEM, c->esp + 8), rt_r32(G_MEM, c->esp + 12), rt_r32(G_MEM, c->esp + 16));
        if (getenv("DS_HOOKTRACE") && atoi(getenv("DS_HOOKTRACE")) >= 2)   /* and 80 bytes at the first two stack arguments */
            for (int k = 1; k <= 2; k++) {
                uint32_t p = rt_r32(G_MEM, c->esp + 4 * (uint32_t)k); if (p < 0x10000) continue;
                fprintf(stderr, "   arg%d:", k); for (uint32_t j = 0; j < 80; j++) fprintf(stderr, " %02x", G_MEM[p + j]); fprintf(stderr, "\n");
            }
    }
}
int native_override(Ctx *c, uint32_t addr);   /* native.c */
int rt_override(Ctx *c, uint32_t addr)
{
    if (native_override(c, addr)) return 1;
    switch (addr) {
    }
    return 0;
}
