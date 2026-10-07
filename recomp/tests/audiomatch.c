/* audiomatch <reference.wav> <capture.wav> [window seconds] [offset seconds]: does the game's recorded output
 * (W32_AUDIO_DUMP: 16-bit stereo) contain the reference sound (any 16-bit PCM WAV, e.g. decoded from the game's
 * archives by tests/mpq.py)? A window of the reference (offset s in, window s long) is resampled to the capture's
 * rate, found in the capture (coarse search on decimated signals, then refined sample by sample), and compared:
 *   lag (s into the capture), correlation (1 = identical shape), gain (capture / reference level).
 * Prints "match corr=0.97 lag=12.345 gain=0.42" and exits 0 when the correlation is at least 0.8, else 1. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float *x; long n; int rate; } Sig;
static int load(const char *path, Sig *s)        /* mono, as floats */
{
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "%s: cannot open\n", path); return -1; }
    uint8_t h[12]; if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fprintf(stderr, "%s: not a WAV\n", path); fclose(f); return -1; }
    int ch = 0, bits = 0, rate = 0, fmt = 0; uint8_t c[8];
    while (fread(c, 1, 8, f) == 8) {
        uint32_t len = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t b[16]; if (len < 16 || fread(b, 1, 16, f) != 16) break;
            fmt = b[0] | b[1] << 8; ch = b[2] | b[3] << 8; rate = b[4] | b[5] << 8 | b[6] << 16 | b[7] << 24; bits = b[14] | b[15] << 8;
            fseek(f, len - 16 + (len & 1), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            if (fmt != 1 || bits != 16 || ch < 1 || ch > 2) { fprintf(stderr, "%s: not 16-bit PCM (format %d, %d bits, %d ch)\n", path, fmt, bits, ch); fclose(f); return -1; }
            long frames = (long)len / (2 * ch); int16_t *raw = malloc((size_t)len);
            frames = (long)fread(raw, (size_t)(2 * ch), (size_t)frames, f);
            s->x = malloc(sizeof(float) * (size_t)frames); s->n = frames; s->rate = rate;
            for (long i = 0; i < frames; i++) s->x[i] = ch == 2 ? (raw[2 * i] + raw[2 * i + 1]) / 65536.0f : raw[i] / 32768.0f;
            free(raw); fclose(f); return 0;
        } else fseek(f, len + (len & 1), SEEK_CUR);
    }
    fprintf(stderr, "%s: no data\n", path); fclose(f); return -1;
}
static float *resample(const Sig *s, int rate, long *n)        /* linear */
{
    *n = (long)((double)s->n * rate / s->rate); float *o = malloc(sizeof(float) * (size_t)*n);
    for (long i = 0; i < *n; i++) {
        double t = (double)i * s->rate / rate; long a = (long)t; double f = t - a;
        o[i] = (float)(a + 1 < s->n ? s->x[a] * (1 - f) + s->x[a + 1] * f : s->x[a < s->n ? a : s->n - 1]);
    }
    return o;
}
static float *decimate(const float *x, long n, int d, long *m)
{
    *m = n / d; float *o = calloc((size_t)*m, sizeof(float));
    for (long i = 0; i < *m; i++) { double a = 0; for (int k = 0; k < d; k++) a += x[i * d + k]; o[i] = (float)(a / d); }
    return o;
}
static double ncc(const float *a, const float *b, long n, double *gain)   /* normalised cross-correlation (no mean removal: audio) */
{
    double ab = 0, aa = 0, bb = 0;
    for (long i = 0; i < n; i++) { ab += (double)a[i] * b[i]; aa += (double)a[i] * a[i]; bb += (double)b[i] * b[i]; }
    if (gain) *gain = aa > 0 ? sqrt(bb / aa) : 0;
    return aa > 0 && bb > 0 ? ab / sqrt(aa * bb) : 0;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: audiomatch <reference.wav> <capture.wav> [window s] [offset s]\n"); return 2; }
    Sig ref, cap; if (load(argv[1], &ref) || load(argv[2], &cap)) return 2;
    double win_s = argc > 3 ? atof(argv[3]) : 4, off_s = argc > 4 ? atof(argv[4]) : 0.5;
    long rn; float *r = resample(&ref, cap.rate, &rn);
    long w0 = (long)(off_s * cap.rate), wn = (long)(win_s * cap.rate);
    if (w0 + wn > rn) { w0 = 0; wn = rn < wn ? rn : wn; }
    if (wn > cap.n) { fprintf(stderr, "capture shorter than the window\n"); return 1; }
    const int D = 32; long rd_n, cd_n; float *rd = decimate(r + w0, wn, D, &rd_n), *cd = decimate(cap.x, cap.n, D, &cd_n);
    long best = 0; double bc = -2;
    for (long lag = 0; lag + rd_n <= cd_n; lag++) { double c = ncc(rd, cd + lag, rd_n, 0); if (c > bc) { bc = c; best = lag; } }
    long fine = best * D, fbest = fine; double fc = -2, gain = 0;
    for (long lag = fine - 2 * D; lag <= fine + 2 * D; lag++) {
        if (lag < 0 || lag + wn > cap.n) continue;
        double g, c = ncc(r + w0, cap.x + lag, wn, &g); if (c > fc) { fc = c; fbest = lag; gain = g; }
    }
    printf("%s corr=%.3f lag=%.3f gain=%.3f (reference %.1f-%.1f s, coarse %.3f)\n", fc >= 0.8 ? "match" : "NO MATCH", fc,
           (double)(fbest - w0) / cap.rate, gain, off_s, off_s + win_s, bc);
    return fc >= 0.8 ? 0 : 1;
}
