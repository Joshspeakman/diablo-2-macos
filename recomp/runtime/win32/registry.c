/* ADVAPI32 registry: an in-memory tree, kept in <data>/registry.txt between sessions (Diablo II keeps its options there:
 * volumes, gamma, the last character, the video mode). */
#include "w32.h"

typedef struct RVal { char name[128]; uint32_t type, size; uint8_t *data; } RVal;
typedef struct RKey { char path[512]; RVal *v; int nv; } RKey;
static RKey *keys[512]; static int nkeys;
static pthread_mutex_t rlock = PTHREAD_MUTEX_INITIALIZER;
typedef struct RHandle { RKey *k; } RHandle;

static const char *root_name(uint32_t h)
{
    switch (h) { case 0x80000000u: return "hkcr"; case 0x80000001u: return "hkcu"; case 0x80000002u: return "hklm"; case 0x80000003u: return "hku"; }
    return 0;
}
static RKey *key_of(uint32_t h);
static int full_path(uint32_t parent, const char *sub, char *out, size_t cap)
{
    const char *r = root_name(parent); RKey *pk = r ? 0 : key_of(parent);
    if (!r && !pk) return -1;
    snprintf(out, cap, "%s%s%s", r ? r : pk->path, sub && *sub ? "\\" : "", sub ? sub : "");
    for (char *s = out; *s; s++) { if (*s >= 'A' && *s <= 'Z') *s += 32; if (*s == '/') *s = '\\'; }
    size_t l = strlen(out); while (l && out[l - 1] == '\\') out[--l] = 0;
    return 0;
}
static RKey *lookup(const char *path, int create)
{
    for (int k = 0; k < nkeys; k++) if (!strcmp(keys[k]->path, path)) return keys[k];
    if (!create || nkeys == 512) return 0;
    RKey *k = calloc(1, sizeof *k); snprintf(k->path, sizeof k->path, "%s", path); keys[nkeys++] = k;
    return k;
}
/* a string value put in place before the game looks (install-time values; W32_REGISTRY="key|name|value;..." in tests) */
void w32_reg_string(const char *path, const char *name, const char *str)
{
    char p[512]; snprintf(p, sizeof p, "%s", path);
    for (char *q = p; *q; q++) { if (*q >= 'A' && *q <= 'Z') *q += 32; if (*q == '/') *q = '\\'; }
    pthread_mutex_lock(&rlock);
    RKey *k = lookup(p, 1);
    if (k) {
        RVal *v = 0; for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, name)) v = &k->v[j];
        if (!v) { k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); v = &k->v[k->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "%s", name); }
        free(v->data); v->type = 1; v->size = (uint32_t)strlen(str) + 1; v->data = (uint8_t *)strdup(str);   /* REG_SZ */
    }
    pthread_mutex_unlock(&rlock);
}
/* ---- the registry between sessions: <data>/registry.txt, one value per line: key<TAB>name<TAB>type<TAB>hex data ---- */
static char reg_file[1200]; static int reg_dirty;
static RVal *val_put(RKey *k, const char *name)             /* (rlock held) the value, created empty if new */
{
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, name)) return &k->v[j];
    k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); RVal *v = &k->v[k->nv++]; memset(v, 0, sizeof *v);
    snprintf(v->name, sizeof v->name, "%s", name); return v;
}
static void reg_save(void)                                  /* (rlock held) */
{
    if (!*reg_file || !reg_dirty) return;
    char tmp[1300]; snprintf(tmp, sizeof tmp, "%s.new", reg_file);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    for (int k = 0; k < nkeys; k++) for (int j = 0; j < keys[k]->nv; j++) {
        RVal *v = &keys[k]->v[j];
        fprintf(f, "%s\t%s\t%u\t", keys[k]->path, v->name, v->type);
        for (uint32_t b = 0; b < v->size; b++) fprintf(f, "%02x", v->data[b]);
        fputc('\n', f);
    }
    if (fclose(f) == 0 && rename(tmp, reg_file) == 0) reg_dirty = 0;
}
static void reg_load(void)
{
    FILE *f = fopen(reg_file, "r"); if (!f) return;
    char line[8192];
    while (fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : 0, *t3 = t2 ? strchr(t2 + 1, '\t') : 0;
        if (!t3) continue;
        *t1 = *t2 = *t3 = 0; char *hex = t3 + 1; size_t hl = strcspn(hex, "\r\n"); hex[hl] = 0;
        RKey *k = lookup(line, 1); if (!k) continue;
        RVal *v = val_put(k, t1 + 1); free(v->data);
        v->type = (uint32_t)strtoul(t2 + 1, 0, 10); v->size = (uint32_t)(hl / 2); v->data = malloc(v->size ? v->size : 1);
        for (uint32_t b = 0; b < v->size; b++) { unsigned x; sscanf(hex + 2 * b, "%2x", &x); v->data[b] = (uint8_t)x; }
    }
    fclose(f);
}
/* The registry at start-up: what was saved last time, then what Diablo II's installer would have written (where the
 * game is; it has never run that installer here). The values the game itself writes (options, last character) are
 * kept as it left them. */
void w32_registry_install(const char *data_dir)
{
    pthread_mutex_lock(&rlock);
    snprintf(reg_file, sizeof reg_file, "%s/registry.txt", data_dir);
    reg_load();
    static const char *const inst[][3] = {
        {"hklm\\software\\blizzard entertainment\\diablo ii", "InstallPath", "C:\\Program Files\\Diablo II"},
        {"hkcu\\software\\blizzard entertainment\\diablo ii", "InstallPath", "C:\\Program Files\\Diablo II"},
        {"hkcu\\control panel\\international", "sLanguage", "ENU"},   /* the game's language check reads it */
    };
    for (unsigned j = 0; j < sizeof inst / sizeof inst[0]; j++) {
        RKey *k = lookup(inst[j][0], 1); RVal *v = k ? val_put(k, inst[j][1]) : 0;
        if (v && !v->data) { v->type = 1; v->size = (uint32_t)strlen(inst[j][2]) + 1; v->data = (uint8_t *)strdup(inst[j][2]); }
    }
    /* the video mode, until the player picks another: DirectDraw (VideoConfig\Render 0; 1 is Direct3D, 3 Glide), the
     * game's own software renderer, whose 8-bit frames this port shows (runtime/win32/ddraw_pal.c) */
    {
        const char *forced = getenv("D2_RENDER");                 /* tests: a given mode */
        static const char *const vc[] = {"hklm\\software\\blizzard entertainment\\diablo ii\\videoconfig",
                                         "hkcu\\software\\blizzard entertainment\\diablo ii\\videoconfig"};
        for (int j = 0; j < 2; j++) {
            RKey *k = lookup(vc[j], 1); RVal *v = k ? val_put(k, "Render") : 0;
            if (v && (!v->data || forced)) { uint32_t m = forced ? (uint32_t)atoi(forced) : 0; free(v->data); v->type = 4; v->size = 4; v->data = malloc(4); memcpy(v->data, &m, 4); }
        }
    }
    /* Direct3D's view, when the launch window sets it (D2_PERSPECTIVE on/off): the game's Options -> Video -> Perspective */
    {
        const char *per = getenv("D2_PERSPECTIVE");
        RKey *k = per && (!strcmp(per, "on") || !strcmp(per, "off")) ? lookup("hkcu\\software\\blizzard entertainment\\diablo ii", 1) : 0;
        RVal *v = k ? val_put(k, "Perspective") : 0;
        if (v) { uint32_t m = !strcmp(per, "on"); free(v->data); v->type = 4; v->size = 4; v->data = malloc(4); memcpy(v->data, &m, 4); }
    }
    pthread_mutex_unlock(&rlock);
}
static void reg_seed(void)
{
    static int done; if (done) return; done = 1;
    const char *e = getenv("W32_REGISTRY"); if (!e || !getenv("DS_TEST")) return;
    char *s = strdup(e), *save, *t = strtok_r(s, ";", &save);
    for (; t; t = strtok_r(0, ";", &save)) { char *a = strchr(t, '|'), *b = a ? strchr(a + 1, '|') : 0; if (!b) continue; *a = *b = 0; w32_reg_string(t, a + 1, b + 1); }
    free(s);
}
#define HREG_BASE 0x00a00000u
static RKey *rh[256];
static RKey *key_of(uint32_t h) { uint32_t i = (h - HREG_BASE) / 4; return (h >= HREG_BASE && i < 256) ? rh[i] : 0; }
static uint32_t new_handle(RKey *k) { for (int i = 1; i < 256; i++) if (!rh[i]) { rh[i] = k; return HREG_BASE + 4 * (uint32_t)i; } return 0; }

IMPL(advapi32, RegOpenKeyExA)
{
    char p[1024]; uint32_t out = ARG(4);
    reg_seed();
    pthread_mutex_lock(&rlock);
    RKey *k = full_path(ARG(0), ARG(1) ? GS(ARG(1)) : "", p, sizeof p) ? 0 : lookup(p, 0);
    uint32_t h = k ? new_handle(k) : 0;
    pthread_mutex_unlock(&rlock);
    if (!k) { if (getenv("W32_REGLOG")) fprintf(stderr, "w32: registry key %s not found\n", p); RET(2, 5); }   /* ERROR_FILE_NOT_FOUND */
    rt_w32(G_MEM, out, h); RET(0, 5);
}
IMPL(advapi32, RegCreateKeyExA)
{
    char p[1024]; uint32_t out = ARG(7), disp = ARG(8);
    pthread_mutex_lock(&rlock);
    int existed = 0; RKey *k = 0;
    if (!full_path(ARG(0), GS(ARG(1)), p, sizeof p)) { existed = lookup(p, 0) != 0; k = lookup(p, 1); }
    uint32_t h = k ? new_handle(k) : 0;
    pthread_mutex_unlock(&rlock);
    if (!k) RET(2, 9);
    rt_w32(G_MEM, out, h); if (disp) rt_w32(G_MEM, disp, existed ? 2 : 1);
    RET(0, 9);
}
IMPL(advapi32, RegCloseKey) { uint32_t i = (ARG(0) - HREG_BASE) / 4; if (ARG(0) >= HREG_BASE && i < 256) rh[i] = 0; RET(0, 1); }
IMPL(advapi32, RegQueryValueExA)
{
    uint32_t name = ARG(1), type = ARG(3), data = ARG(4), szp = ARG(5);
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 6);
    const char *n = name ? GS(name) : "";
    if (getenv("W32_REGLOG")) fprintf(stderr, "w32: registry read %s\\%s\n", k->path, n);
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) {
        RVal *v = &k->v[j];
        if (type) rt_w32(G_MEM, type, v->type);
        if (data) {
            if (!szp || rt_r32(G_MEM, szp) < v->size) { if (szp) rt_w32(G_MEM, szp, v->size); RET(234, 6); }   /* ERROR_MORE_DATA */
            memcpy(GP(data), v->data, v->size);
        }
        if (szp) rt_w32(G_MEM, szp, v->size);
        RET(0, 6);
    }
    RET(2, 6);
}
IMPL(advapi32, RegSetValueExA)
{
    uint32_t name = ARG(1), type = ARG(3), data = ARG(4), size = ARG(5);
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 6);
    const char *n = name ? GS(name) : "";
    if (getenv("W32_REGLOG")) fprintf(stderr, "w32: registry write %s\\%s (%u bytes, first %08x)\n", k->path, n, size, size >= 4 ? rt_r32(G_MEM, data) : 0);
    pthread_mutex_lock(&rlock);
    RVal *v = 0;
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) v = &k->v[j];
    if (!v) { k->v = realloc(k->v, (size_t)(k->nv + 1) * sizeof *k->v); v = &k->v[k->nv++]; memset(v, 0, sizeof *v); snprintf(v->name, sizeof v->name, "%s", n); }
    free(v->data); v->type = type; v->size = size; v->data = malloc(size ? size : 1); memcpy(v->data, GP(data), size);
    reg_dirty = 1; reg_save();
    pthread_mutex_unlock(&rlock);
    RET(0, 6);
}
/* (added for Dungeon Siege II) */
static int is_child(const char *parent, const char *path, const char **name)   /* path is a direct subkey of parent */
{
    size_t l = strlen(parent);
    if (strncmp(path, parent, l) || path[l] != '\\' || strchr(path + l + 1, '\\')) return 0;
    *name = path + l + 1; return 1;
}
IMPL(advapi32, RegDeleteValueA)
{
    RKey *k = key_of(ARG(0)); if (!k) RET(6, 2);
    const char *n = ARG(1) ? GS(ARG(1)) : "";
    pthread_mutex_lock(&rlock);
    for (int j = 0; j < k->nv; j++) if (!strcasecmp(k->v[j].name, n)) {
        free(k->v[j].data); k->v[j] = k->v[--k->nv]; reg_dirty = 1; reg_save(); pthread_mutex_unlock(&rlock); RET(0, 2);
    }
    pthread_mutex_unlock(&rlock); RET(2, 2);
}
IMPL(advapi32, RegDeleteKeyA)                          /* only a key without subkeys, as on Windows */
{
    char p[1024]; const char *nm;
    pthread_mutex_lock(&rlock);
    RKey *k = full_path(ARG(0), GS(ARG(1)), p, sizeof p) ? 0 : lookup(p, 0);
    if (!k) { pthread_mutex_unlock(&rlock); RET(2, 2); }
    for (int j = 0; j < nkeys; j++) if (is_child(p, keys[j]->path, &nm)) { pthread_mutex_unlock(&rlock); RET(5, 2); }
    for (int j = 0; j < nkeys; j++) if (keys[j] == k) { keys[j] = keys[--nkeys]; break; }
    for (int i = 0; i < 256; i++) if (rh[i] == k) rh[i] = 0;
    for (int v = 0; v < k->nv; v++) free(k->v[v].data);
    free(k->v); free(k); reg_dirty = 1; reg_save();
    pthread_mutex_unlock(&rlock); RET(0, 2);
}
IMPL(advapi32, RegEnumKeyExA)                          /* (h, index, name, *namelen, reserved, class, *classlen, *time) */
{
    RKey *k = key_of(ARG(0)); uint32_t idx = ARG(1), name = ARG(2), lenp = ARG(3); const char *nm;
    if (!k) RET(6, 8);
    pthread_mutex_lock(&rlock);
    uint32_t n = 0;
    for (int j = 0; j < nkeys; j++) if (is_child(k->path, keys[j]->path, &nm) && n++ == idx) {
        uint32_t l = (uint32_t)strlen(nm);
        if (rt_r32(G_MEM, lenp) <= l) { pthread_mutex_unlock(&rlock); RET(234, 8); }   /* ERROR_MORE_DATA */
        memcpy(GP(name), nm, l + 1); rt_w32(G_MEM, lenp, l);
        if (ARG(6)) rt_w32(G_MEM, ARG(6), 0);
        if (ARG(7)) { rt_w32(G_MEM, ARG(7), 0); rt_w32(G_MEM, ARG(7) + 4, 0); }
        pthread_mutex_unlock(&rlock); RET(0, 8);
    }
    pthread_mutex_unlock(&rlock); RET(259, 8);         /* ERROR_NO_MORE_ITEMS */
}
IMPL(advapi32, RegQueryInfoKeyA)                       /* (h, class, *classlen, reserved, *subkeys, *maxsubkeylen, *maxclasslen,
                                                        *values, *maxvaluenamelen, *maxvaluelen, *secdesc, *time) */
{
    RKey *k = key_of(ARG(0)); const char *nm; if (!k) RET(6, 12);
    pthread_mutex_lock(&rlock);
    uint32_t nsub = 0, maxsub = 0, maxvn = 0, maxv = 0;
    for (int j = 0; j < nkeys; j++) if (is_child(k->path, keys[j]->path, &nm)) { nsub++; if (strlen(nm) > maxsub) maxsub = (uint32_t)strlen(nm); }
    for (int v = 0; v < k->nv; v++) { if (strlen(k->v[v].name) > maxvn) maxvn = (uint32_t)strlen(k->v[v].name); if (k->v[v].size > maxv) maxv = k->v[v].size; }
    pthread_mutex_unlock(&rlock);
    uint32_t out[] = {ARG(4), nsub, ARG(5), maxsub, ARG(6), 0, ARG(7), (uint32_t)k->nv, ARG(8), maxvn, ARG(9), maxv, ARG(10), 0};
    for (int i = 0; i < 14; i += 2) if (out[i]) rt_w32(G_MEM, out[i], out[i + 1]);
    if (ARG(2)) rt_w32(G_MEM, ARG(2), 0);
    if (ARG(11)) { rt_w32(G_MEM, ARG(11), 0); rt_w32(G_MEM, ARG(11) + 4, 0); }
    RET(0, 12);
}

/* ---- (added for Diablo II) ---- */
IMPL(advapi32, RegFlushKey) { pthread_mutex_lock(&rlock); reg_save(); pthread_mutex_unlock(&rlock); RET(0, 1); }
static uint32_t open_key3(Ctx *c, int create)          /* RegOpenKeyA / RegCreateKeyA (h, sub, *out) */
{
    char p[1024]; uint32_t out = ARG(2);
    reg_seed();
    pthread_mutex_lock(&rlock);
    RKey *k = full_path(ARG(0), ARG(1) ? GS(ARG(1)) : "", p, sizeof p) ? 0 : lookup(p, create);
    uint32_t h = k ? new_handle(k) : 0;
    pthread_mutex_unlock(&rlock);
    if (!k) { if (getenv("W32_REGLOG")) fprintf(stderr, "w32: registry key %s not found\n", p); return 2; }
    rt_w32(G_MEM, out, h); return 0;
}
IMPL(advapi32, RegOpenKeyA) { RET(open_key3(c, 0), 3); }
IMPL(advapi32, RegCreateKeyA) { RET(open_key3(c, 1), 3); }
IMPL(advapi32, RegEnumValueA)                          /* (h, index, name, *namelen, reserved, *type, data, *datalen) */
{
    RKey *k = key_of(ARG(0)); uint32_t idx = ARG(1), name = ARG(2), lenp = ARG(3), type = ARG(5), data = ARG(6), dlen = ARG(7);
    if (!k) RET(6, 8);
    pthread_mutex_lock(&rlock);
    if (idx >= (uint32_t)k->nv) { pthread_mutex_unlock(&rlock); RET(259, 8); }   /* ERROR_NO_MORE_ITEMS */
    RVal *v = &k->v[idx]; uint32_t l = (uint32_t)strlen(v->name);
    if (rt_r32(G_MEM, lenp) <= l) { pthread_mutex_unlock(&rlock); RET(234, 8); }
    memcpy(GP(name), v->name, l + 1); rt_w32(G_MEM, lenp, l);
    if (type) rt_w32(G_MEM, type, v->type);
    if (data) { if (!dlen || rt_r32(G_MEM, dlen) < v->size) { if (dlen) rt_w32(G_MEM, dlen, v->size); pthread_mutex_unlock(&rlock); RET(234, 8); }
                memcpy(GP(data), v->data, v->size); }
    if (dlen) rt_w32(G_MEM, dlen, v->size);
    pthread_mutex_unlock(&rlock); RET(0, 8);
}
