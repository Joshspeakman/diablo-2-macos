/* Windows functions Diablo II 1.14d (and the Smacker library it ships) imports beyond the other ports' set.
 * Signature checks (crypt32), access tokens, process and module lists, consoles and I/O completion ports answer
 * "not available" as on a locked-down PC; nothing here reports a trust or a privilege the game doesn't have. */
#include "w32.h"
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>

static uint32_t scratch(uint32_t n) { return heap_alloc(w32_process_heap, 8, n); }
static void unscratch(uint32_t p) { if (p) heap_free(w32_process_heap, p); }
static uint32_t ansi_of(uint32_t w)
{
    if (!w) return 0;
    int n = w32_wide_to_mb(0, (const uint16_t *)GP(w), -1, 0, 0); uint32_t a = scratch((uint32_t)n + 1);
    w32_wide_to_mb(0, (const uint16_t *)GP(w), -1, (uint8_t *)GP(a), n + 1); return a;
}
#define VIA_ANSI(fn, mask) do { uint32_t tmp[8] = {0}; void fn(Ctx *); for (int k = 0; k < 8; k++) if ((mask) >> k & 1) { tmp[k] = ansi_of(ARG(k)); rt_w32(G_MEM, c->esp + 4 + 4 * k, tmp[k]); } \
                                fn(c); for (int k = 0; k < 8; k++) unscratch(tmp[k]); } while (0)
#define NA(n, v, err) { w32_set_last_error(c, err); RET(v, n); }

/* ---- advapi32: security identifiers and tokens (an ordinary user: not in the Administrators group) ---- */
IMPL(advapi32, AllocateAndInitializeSid) { if (ARG(10)) rt_w32(G_MEM, ARG(10), 0); NA(11, 0, 120) }   /* ERROR_CALL_NOT_IMPLEMENTED */
IMPL(advapi32, FreeSid) { RET(0, 1); }
IMPL(advapi32, CheckTokenMembership) { if (ARG(2)) rt_w32(G_MEM, ARG(2), 0); RET(1, 3); }

/* ---- crypt32: no certificate stores and no signed-file queries ---- */
IMPL(crypt32, CryptQueryObject) NA(11, 0, 0x80092009u)  /* CRYPT_E_NO_MATCH */
IMPL(crypt32, CryptMsgGetParam) NA(5, 0, 0x8009200du)
IMPL(crypt32, CryptMsgClose) { RET(1, 1); }
IMPL(crypt32, CertFindCertificateInStore) NA(6, 0, 0x80092004u)   /* CRYPT_E_NOT_FOUND */
IMPL(crypt32, CertFreeCertificateContext) { RET(1, 1); }
IMPL(crypt32, CertCloseStore) { RET(1, 2); }
IMPL(crypt32, CertGetNameStringW) { if (ARG(4) && ARG(5)) rt_w16(G_MEM, ARG(4), 0); RET(1, 6); }   /* an empty name (count includes the terminator) */

/* ---- imm32 ---- */
IMPL(imm32, ImmGetCandidateListCountA) { if (ARG(1)) rt_w32(G_MEM, ARG(1), 0); RET(0, 2); }

/* ---- kernel32 ---- */
extern char w32_cwd[1024];
IMPL(kernel32, SetCurrentDirectoryA)
{
    const char *p = GS(ARG(0)); char host[2048];
    if (w32_host_path(p, host, sizeof host, 0)) NA(1, 0, 3)
    struct stat st; if (stat(host, &st) || !S_ISDIR(st.st_mode)) NA(1, 0, 3)   /* ERROR_PATH_NOT_FOUND */
    char full[1024];
    if (((p[0] | 32) >= 'a' && (p[0] | 32) <= 'z') && p[1] == ':') snprintf(full, sizeof full, "%s", p);
    else if (p[0] == '\\') snprintf(full, sizeof full, "C:%s", p);
    else snprintf(full, sizeof full, "%s\\%s", w32_cwd, p);
    size_t l = strlen(full); while (l > 3 && (full[l - 1] == '\\' || full[l - 1] == '/')) full[--l] = 0;
    snprintf(w32_cwd, sizeof w32_cwd, "%s", full); RET(1, 1);
}
/* no .ini files: every lookup gets its default */
IMPL(kernel32, GetPrivateProfileStringA)               /* (section, key, default, out, size, file) */
{
    uint32_t out = ARG(3), cap = ARG(4); const char *d = ARG(2) ? GS(ARG(2)) : "";
    if (!cap) RET(0, 6);
    uint32_t n = (uint32_t)strlen(d); if (n >= cap) n = cap - 1;
    memcpy(GP(out), d, n); rt_w8(G_MEM, out + n, 0); RET(n, 6);
}
IMPL(kernel32, GetPrivateProfileIntA) { RET(ARG(2), 4); }
IMPL(kernel32, GetProfileIntA) { RET(ARG(2), 3); }
/* no console */
IMPL(kernel32, GetConsoleCP) { RET(0, 0); }
IMPL(kernel32, GetConsoleOutputCP) { RET(0, 0); }
IMPL(kernel32, GetConsoleMode) NA(2, 0, 6)
IMPL(kernel32, WriteConsoleA) { if (ARG(3)) rt_w32(G_MEM, ARG(3), 0); NA(5, 0, 6) }
IMPL(kernel32, WriteConsoleW) { if (ARG(3)) rt_w32(G_MEM, ARG(3), 0); NA(5, 0, 6) }
/* process and module lists: CreateToolhelp32Snapshot already refuses, so these never see a snapshot */
IMPL(kernel32, Process32First) NA(2, 0, 18)            /* ERROR_NO_MORE_FILES */
IMPL(kernel32, Process32Next) NA(2, 0, 18)
IMPL(kernel32, Process32FirstW) NA(2, 0, 18)
IMPL(kernel32, Process32NextW) NA(2, 0, 18)
IMPL(kernel32, Module32FirstW) NA(2, 0, 18)
IMPL(kernel32, Module32NextW) NA(2, 0, 18)
IMPL(kernel32, OpenThread) NA(3, 0, 5)
IMPL(kernel32, GetProcessId) { RET(0x40, 1); }         /* (the same id GetCurrentProcessId gives) */
IMPL(kernel32, GetExitCodeProcess) { if (ARG(1)) rt_w32(G_MEM, ARG(1), 0); RET(1, 2); }
IMPL(kernel32, ReadProcessMemory)                      /* (process, address, buffer, size, *read): the game's own memory only */
{
    uint32_t h = ARG(0), a = ARG(1), buf = ARG(2), n = ARG(3), rd = ARG(4);
    if ((h != 0xffffffffu && h != 0x40) || !vm_committed(a, n)) { if (rd) rt_w32(G_MEM, rd, 0); NA(5, 0, 299) }   /* ERROR_PARTIAL_COPY */
    memmove(GP(buf), GP(a), n); if (rd) rt_w32(G_MEM, rd, n); RET(1, 5);
}
IMPL(kernel32, CreateIoCompletionPort) NA(4, 0, 120)
IMPL(kernel32, GetQueuedCompletionStatus) NA(5, 0, 6)
IMPL(kernel32, GetDiskFreeSpaceA)                      /* (root, *sectors per cluster, *bytes per sector, *free clusters, *clusters) */
{
    struct statfs fs; uint64_t fr = 64ull << 30, tot = 256ull << 30;
    if (!statfs(w32_game_dir, &fs)) { fr = (uint64_t)fs.f_bavail * fs.f_bsize; tot = (uint64_t)fs.f_blocks * fs.f_bsize; }
    uint64_t cl = 8 * 512; uint32_t frc = fr / cl > 0x7fffffffu ? 0x7fffffffu : (uint32_t)(fr / cl), totc = tot / cl > 0x7fffffffu ? 0x7fffffffu : (uint32_t)(tot / cl);
    uint32_t out[] = {ARG(1), 8, ARG(2), 512, ARG(3), frc, ARG(4), totc};
    for (int i = 0; i < 8; i += 2) if (out[i]) rt_w32(G_MEM, out[i], out[i + 1]);
    RET(1, 5);
}
IMPL(kernel32, RtlCaptureContext) { (void)c; RET(0, 1); }   /* only the C runtime's buffer-overrun report uses it */
IMPL(kernel32, IsBadStringPtrA) { uint32_t p = ARG(0); RET(!p || !vm_committed(p, 1), 2); }
IMPL(kernel32, GetSystemDefaultLangID) { RET(0x409, 0); }
IMPL(kernel32, GetSystemDefaultLCID) { RET(0x409, 0); }
IMPL(kernel32, InterlockedCompareExchange)             /* (*dest, exchange, comparand) -> the old value */
{
    uint32_t want = ARG(2); __atomic_compare_exchange_n((uint32_t *)GP(ARG(0)), &want, ARG(1), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    RET(want, 3);
}
IMPL(kernel32, LoadModule) { RET(2, 2); }             /* ERROR_FILE_NOT_FOUND (< 32: failed) */
/* wide-character versions: the ANSI implementations */
IMPL(kernel32, LoadLibraryW) { VIA_ANSI(imp_kernel32_LoadLibraryA, 1); }
IMPL(kernel32, GetModuleHandleW) { VIA_ANSI(imp_kernel32_GetModuleHandleA, 1); }
IMPL(kernel32, GetFileAttributesW) { VIA_ANSI(imp_kernel32_GetFileAttributesA, 1); }
IMPL(kernel32, CreateEventW) { VIA_ANSI(imp_kernel32_CreateEventA, 8); }
IMPL(kernel32, CopyFileW) { VIA_ANSI(imp_kernel32_CopyFileA, 3); }
IMPL(kernel32, CreateProcessW) { VIA_ANSI(imp_kernel32_CreateProcessA, 0x83); }   /* (application, command line, ..., directory) */
IMPL(kernel32, GetTempPathW)
{
    static const char t[] = "C:\\Temp\\"; uint32_t cap = ARG(0), out = ARG(1), n = sizeof t - 1;
    if (cap <= n) RET(n + 1, 2);
    for (uint32_t k = 0; k <= n; k++) rt_w16(G_MEM, out + 2 * k, (uint8_t)t[k]);
    RET(n, 2);
}

/* ---- psapi ---- */
static uint32_t image_size_at(uint32_t base) { uint32_t pe = base + rt_r32(G_MEM, base + 0x3c); return rt_r32(G_MEM, pe + 24 + 56); }
IMPL(psapi, GetModuleInformation)                      /* (process, module, MODULEINFO *, size) */
{
    uint32_t m = ARG(1) ? ARG(1) : w32_image_base, out = ARG(2);
    if (!out || ARG(3) < 12 || rt_r16(G_MEM, m) != 0x5a4d) NA(4, 0, 87)
    uint32_t pe = m + rt_r32(G_MEM, m + 0x3c);
    rt_w32(G_MEM, out, m); rt_w32(G_MEM, out + 4, image_size_at(m)); rt_w32(G_MEM, out + 8, m + rt_r32(G_MEM, pe + 24 + 16));
    RET(1, 4);
}
IMPL(psapi, GetModuleFileNameExA)                      /* (process, module, buffer, size): this process's GetModuleFileNameA */
{
    uint32_t ret = rt_r32(G_MEM, c->esp);
    c->esp += 4; rt_w32(G_MEM, c->esp, ret);           /* drop the process argument: (module, buffer, size) remain */
    void imp_kernel32_GetModuleFileNameA(Ctx *); imp_kernel32_GetModuleFileNameA(c);
}

/* ---- shell32 ---- */
IMPL(shell32, SHGetFolderPathA) { void imp_shfolder_SHGetFolderPathA(Ctx *); imp_shfolder_SHGetFolderPathA(c); }
IMPL(shell32, SHAppBarMessage) { RET(0, 2); }          /* no taskbar */
IMPL(shell32, ShellExecuteA)                           /* (hwnd, verb, file, params, dir, show): opening documents or web pages is not done */
{
    fprintf(stderr, "w32: ShellExecuteA(%s) not done\n", ARG(2) ? GS(ARG(2)) : "");
    RET(2, 6);                                         /* SE_ERR_FNF */
}

/* ---- user32: rectangles ---- */
static void rect_get(uint32_t p, int32_t *r) { for (int k = 0; k < 4; k++) r[k] = (int32_t)rt_r32(G_MEM, p + 4 * (uint32_t)k); }
static void rect_put(uint32_t p, const int32_t *r) { for (int k = 0; k < 4; k++) rt_w32(G_MEM, p + 4 * (uint32_t)k, (uint32_t)r[k]); }
IMPL(user32, SetRect) { if (!ARG(0)) RET(0, 5); int32_t r[4] = {(int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4)}; rect_put(ARG(0), r); RET(1, 5); }
IMPL(user32, CopyRect) { if (!ARG(0) || !ARG(1)) RET(0, 2); int32_t r[4]; rect_get(ARG(1), r); rect_put(ARG(0), r); RET(1, 2); }
IMPL(user32, PtInRect)                                 /* (rect, POINT by value: x, y) */
{
    int32_t r[4]; rect_get(ARG(0), r); int32_t x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    RET(x >= r[0] && x < r[2] && y >= r[1] && y < r[3], 3);
}
IMPL(user32, IntersectRect)
{
    int32_t a[4], b[4], o[4]; rect_get(ARG(1), a); rect_get(ARG(2), b);
    o[0] = a[0] > b[0] ? a[0] : b[0]; o[1] = a[1] > b[1] ? a[1] : b[1]; o[2] = a[2] < b[2] ? a[2] : b[2]; o[3] = a[3] < b[3] ? a[3] : b[3];
    if (o[0] >= o[2] || o[1] >= o[3]) { memset(o, 0, sizeof o); rect_put(ARG(0), o); RET(0, 3); }
    rect_put(ARG(0), o); RET(1, 3);
}
IMPL(user32, AdjustWindowRectEx) { RET(1, 4); }        /* the game's window has no frame here: client = window */
/* ---- user32: the rest ---- */
extern int w32_screen_w, w32_screen_h;
IMPL(user32, MonitorFromWindow) { RET(0x00c00001u, 2); }   /* the one monitor */
IMPL(user32, GetMonitorInfoA)                          /* (monitor, MONITORINFO: size, rcMonitor, rcWork, flags [, device name]) */
{
    uint32_t mi = ARG(1); if (!mi) RET(0, 2);
    int32_t r[4] = {0, 0, w32_screen_w, w32_screen_h}; rect_put(mi + 4, r); rect_put(mi + 20, r); rt_w32(G_MEM, mi + 36, 1);   /* MONITORINFOF_PRIMARY */
    if (rt_r32(G_MEM, mi) >= 72) memcpy(GP(mi + 40), "\\\\.\\DISPLAY1", 13);
    RET(1, 2);
}
IMPL(user32, GetWindowPlacement)                       /* (hwnd, WINDOWPLACEMENT): shown normally, filling the screen */
{
    uint32_t p = ARG(1); if (!p) RET(0, 2);
    rt_w32(G_MEM, p + 4, 0); rt_w32(G_MEM, p + 8, 1);   /* flags, SW_SHOWNORMAL */
    for (uint32_t k = 12; k < 28; k += 4) rt_w32(G_MEM, p + k, 0);
    int32_t r[4] = {0, 0, w32_screen_w, w32_screen_h}; rect_put(p + 28, r); RET(1, 2);
}
IMPL(user32, CharLowerBuffA) { uint8_t *s = GP(ARG(0)); uint32_t n = ARG(1); for (uint32_t k = 0; k < n; k++) if (s[k] >= 'A' && s[k] <= 'Z') s[k] += 32; RET(n, 2); }
IMPL(user32, TrackMouseEvent) { RET(1, 1); }
IMPL(user32, IsClipboardFormatAvailable) { RET(0, 1); }
IMPL(user32, LoadAcceleratorsA) { RET(0x00c10001u, 2); }
IMPL(user32, LoadImageA)                               /* (instance, name, type, cx, cy, flags): icons and cursors as LoadIcon/LoadCursor give them */
{
    uint32_t t = ARG(2); RET(t == 2 ? 0x30000 + (ARG(1) & 0xffff) : 0x31000 + (ARG(1) & 0xffff), 6);
}
IMPL(user32, wvsprintfA)                               /* (out, format, va_list): wsprintfA with the arguments elsewhere */
{
    uint32_t w32_format(char *out, size_t cap, const char *fmt, uint32_t args);
    char buf[1025]; uint32_t n = w32_format(buf, sizeof buf, GS(ARG(1)), ARG(2));
    if (n > 1024) n = 1024;
    memcpy(GP(ARG(0)), buf, n + 1); RET(n, 3);
}
IMPL(user32, IsChild) { RET(0, 2); }
IMPL(user32, GetClassInfoA) NA(3, 0, 1411)             /* ERROR_CLASS_DOES_NOT_EXIST */

/* ---- winmm: multimedia timers (Smacker's optional timing callback) ---- */
IMPL(winmm, timeSetEvent) { static int once; if (!once++) fprintf(stderr, "w32: timeSetEvent not available\n"); RET(0, 5); }
IMPL(winmm, timeKillEvent) { RET(0, 1); }

/* ---- wsock32 / ws2_32 ---- */
IMPL(wsock32, ord112) { rt_w32(G_MEM, c->fs_base + 0x34, ARG(0)); RET(0, 1); }   /* WSASetLastError */

/* SHGetKnownFolderPath(folder id, flags, token, PWSTR *out): Vista's folders, the string for CoTaskMemFree (the process
 * heap here). Diablo II 1.14d keeps its characters in <profile>\Saved Games\Diablo II when this exists. */
IMPL(shell32, SHGetKnownFolderPath)
{
    static const struct { uint32_t d1; const char *path; } known[] = {
        {0x4c5c32ffu, "C:\\Users\\player\\Saved Games"},           /* FOLDERID_SavedGames */
        {0xfdd39ad0u, "C:\\Users\\player\\Documents"},             /* FOLDERID_Documents */
        {0x5e6c858fu, "C:\\Users\\player"},                        /* FOLDERID_Profile */
        {0xf1b32785u, "C:\\Users\\player\\AppData\\Local"},        /* FOLDERID_LocalAppData */
        {0x3eb685dbu, "C:\\Users\\player\\AppData\\Roaming"},      /* FOLDERID_RoamingAppData */
        {0x62ab5d82u, "C:\\ProgramData"},                          /* FOLDERID_ProgramData */
    };
    uint32_t id = ARG(0) ? rt_r32(G_MEM, ARG(0)) : 0, out = ARG(3);
    if (out) rt_w32(G_MEM, out, 0);
    for (unsigned k = 0; k < sizeof known / sizeof *known; k++) if (known[k].d1 == id) {
        char host[2048]; if (!w32_host_path(known[k].path, host, sizeof host, 1)) mkdir(host, 0755);   /* KF_FLAG_CREATE, always */
        uint32_t n = (uint32_t)strlen(known[k].path), p = heap_alloc(w32_process_heap, 0, 2 * (n + 1));
        for (uint32_t j = 0; j <= n; j++) rt_w16(G_MEM, p + 2 * j, (uint8_t)known[k].path[j]);
        if (out) rt_w32(G_MEM, out, p);
        RET(0, 4);
    }
    fprintf(stderr, "w32: SHGetKnownFolderPath(%08x-...) unknown\n", id);
    RET(0x80070002u, 4);                               /* HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) */
}
