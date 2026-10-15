// dsmath_test - run every dsm_ function on a fixed input set and print one FNV-1a hash per function.
// The same hashes on Mac arm64, Mac x86_64 and Windows x64 = bit-identical results on these inputs.
// Inputs: integer grid pairs and angles (what the game feeds), plus 200k xorshift values per function
// spread over each function's useful range, plus edge values.
#include "dsmath.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint64_t rs = 0x9E3779B97F4A7C15ULL;
static uint64_t xs(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static double ud(double lo, double hi) { return lo + (hi - lo) * ((double)(xs() >> 11) / 9007199254740992.0); }
static uint64_t H;
static uint64_t nans;
/* NaN bit patterns differ by CPU (x86 default NaN has the sign bit set, arm64 not): hash every NaN as one value */
static void hf(float v) { if (v != v) { nans++; v = __builtin_nanf(""); } uint32_t u; memcpy(&u, &v, 4); for (int i = 0; i < 4; i++) { H ^= (u >> (8 * i)) & 0xFF; H *= 0x100000001B3ULL; } }
static void hd(double v) { if (v != v) { nans++; v = __builtin_nan(""); } uint64_t u; memcpy(&u, &v, 8); for (int i = 0; i < 8; i++) { H ^= (u >> (8 * i)) & 0xFF; H *= 0x100000001B3ULL; } }
#define BEGIN(name) do { H = 0xCBF29CE484222325ULL; rs = 0x9E3779B97F4A7C15ULL ^ (uint64_t)sizeof(name); } while (0)
#define END(name) printf("%-8s %016llx\n", name, (unsigned long long)H)
#define N 200000

static const double EDGE[] = { 0.0, -0.0, 1e-30, -1e-30, 1e-8, 0.5, 1.0, -1.0, 2.0, 3.14159265358979, 1e10, -1e10, 88.0, -88.0, 700.0 };
#define NE (int)(sizeof EDGE / sizeof *EDGE)

#define F1(name, fn, lo, hi) do { BEGIN(name); for (int i = 0; i < NE; i++) hf(fn((float)EDGE[i])); \
    for (int i = 0; i < N; i++) hf(fn((float)ud(lo, hi))); END(name); } while (0)
#define D1(name, fn, lo, hi) do { BEGIN(name); for (int i = 0; i < NE; i++) hd(fn(EDGE[i])); \
    for (int i = 0; i < N; i++) hd(fn(ud(lo, hi))); END(name); } while (0)

int main(void) {
    printf("dsmath %s\n", DSMATH_VERSION);
    BEGIN("atan2f");
    for (int y = -256; y <= 256; y++) for (int x = -256; x <= 256; x++) hf(dsm_atan2f((float)y, (float)x));
    for (int i = 0; i < N; i++) hf(dsm_atan2f((float)ud(-1e4, 1e4), (float)ud(-1e4, 1e4)));
    END("atan2f");
    BEGIN("atan2");
    for (int y = -256; y <= 256; y++) for (int x = -256; x <= 256; x++) hd(dsm_atan2((double)y, (double)x));
    for (int i = 0; i < N; i++) hd(dsm_atan2(ud(-1e4, 1e4), ud(-1e4, 1e4)));
    END("atan2");
    BEGIN("sincosf");
    for (int d = -7200; d <= 7200; d++) { float s, c; dsm_sincosf((float)d * 0.017453292f / 10.0f, &s, &c); hf(s); hf(c); }
    for (int i = 0; i < N; i++) { float s, c; dsm_sincosf((float)ud(-1e3, 1e3), &s, &c); hf(s); hf(c); }
    END("sincosf");
    BEGIN("sincos");
    for (int i = 0; i < N; i++) { double s, c; dsm_sincos(ud(-1e3, 1e3), &s, &c); hd(s); hd(c); }
    END("sincos");
    F1("sinf", dsm_sinf, -1e4, 1e4); F1("cosf", dsm_cosf, -1e4, 1e4); F1("tanf", dsm_tanf, -10, 10);
    F1("atanf", dsm_atanf, -1e3, 1e3); F1("acosf", dsm_acosf, -1, 1); F1("asinf", dsm_asinf, -1, 1);
    F1("expf", dsm_expf, -90, 90); F1("logf", dsm_logf, 0, 1e6); F1("log10f", dsm_log10f, 0, 1e6);
    F1("sinhf", dsm_sinhf, -20, 20); F1("coshf", dsm_coshf, -20, 20); F1("tanhf", dsm_tanhf, -20, 20);
    D1("sin", dsm_sin, -1e4, 1e4); D1("cos", dsm_cos, -1e4, 1e4); D1("tan", dsm_tan, -10, 10);
    D1("atan", dsm_atan, -1e3, 1e3); D1("acos", dsm_acos, -1, 1); D1("asin", dsm_asin, -1, 1);
    D1("exp", dsm_exp, -700, 700); D1("log", dsm_log, 0, 1e9); D1("log10", dsm_log10, 0, 1e9);
    D1("sinh", dsm_sinh, -20, 20); D1("cosh", dsm_cosh, -20, 20); D1("tanh", dsm_tanh, -20, 20);
    BEGIN("powf"); for (int i = 0; i < N; i++) hf(dsm_powf((float)ud(0, 100), (float)ud(-10, 10))); END("powf");
    BEGIN("pow");
    for (int i = 0; i < N; i++) hd(dsm_pow(ud(0, 100), ud(-10, 10)));
    for (int i = 0; i < N; i++) hd(dsm_pow(2.718281828459045, -ud(0, 50)));       /* Edgar annealing */
    for (int i = -500; i <= 500; i++) hd(dsm_pow((double)i, 2.0));
    END("pow");
    BEGIN("hypotf"); for (int i = 0; i < N; i++) hf(dsm_hypotf((float)ud(-1e3, 1e3), (float)ud(-1e3, 1e3))); END("hypotf");
    BEGIN("hypot"); for (int i = 0; i < N; i++) hd(dsm_hypot(ud(-1e3, 1e3), ud(-1e3, 1e3))); END("hypot");
    printf("nan_results %llu\n", (unsigned long long)nans);
    return 0;
}
