// CpuLapack.cpp -- see CpuLapack.h.

#include "sfm/ba/CpuLapack.h"

#include "core/Env.h"

#include <algorithm>
#include <vector>

#if SS_SFM_ACCELERATE
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>
#endif

namespace bacpu {

#if SS_SFM_ACCELERATE

bool lapackEnabled() {
    static const bool on = [] {
        const char* v = spirula::env("SFM_BA_LAPACK");
        return !(v && v[0] == '0' && v[1] == '\0');
    }();
    return on;
}

namespace {

// Where A(c,c) sits in rectangular full packed storage, TRANSR='N', UPLO='U'
// (LAPACK's dtpttf documents the layout): the right half of the columns in
// place, the left half transposed below them.
uint64_t rfpDiag(uint64_t c, uint64_t n) {
    if (n % 2 == 0) {
        const uint64_t k = n / 2, ld = n + 1;
        return c >= k ? (c - k) * ld + c : c * ld + k + 1 + c;
    }
    const uint64_t k = (n + 1) / 2, ld = n;
    return c >= k - 1 ? (c - k + 1) * ld + c : c * ld + k + c;
}

}  // namespace

bool lapackFactor(double* a, double* rfp, uint32_t n, const double* diag, double rel) {
    // A held parameter leaves its row empty, a zero pivot dpftrf refuses. The
    // guard's replacement for it is exact there (nothing below the pivot to
    // scale), so those rows get it up front and are the ones the check skips.
    auto limit = [&](uint32_t c) { return diag ? rel * diag[c] : 1e-30; };
    auto at = [&](uint32_t r, uint32_t c) -> double& { return a[(uint64_t)r * (r + 1) / 2 + c]; };
    std::vector<uint8_t> held(n, 0);
    bool any = false;
    for (uint32_t c = 0; c < n; c++) any |= !(at(c, c) > limit(c));
    if (any) {
        std::vector<uint8_t> used(n, 0);
        for (uint32_t r = 0; r < n; r++)
            for (uint32_t c = 0; c < r; c++)
                if (at(r, c) != 0.0) used[r] = used[c] = 1;
        for (uint32_t c = 0; c < n; c++) held[c] = !used[c] && !(at(c, c) > limit(c));
    }

    // Our row-major packed lower triangle is LAPACK's column-major packed upper
    // one, and its U (A = U^T U) is our L.
    __LAPACK_int N = (__LAPACK_int)n, info = 0;
    dtpttf_("N", "U", &N, a, rfp, &info);
    if (info) return false;
    for (uint32_t c = 0; c < n; c++)
        if (held[c]) rfp[rfpDiag(c, n)] = diag ? std::max(diag[c], 1e-30) : 1e-30;
    dpftrf_("N", "U", &N, rfp, &info);
    if (info) return false;
    for (uint32_t c = 0; c < n; c++) {
        const double d = rfp[rfpDiag(c, n)];
        if (!held[c] && !(d * d > limit(c))) return false;
    }
    dtfttp_("N", "U", &N, rfp, a, &info);
    return info == 0;
}

#else

bool lapackEnabled() { return false; }
bool lapackFactor(double*, double*, uint32_t, const double*, double) { return false; }

#endif

}  // namespace bacpu
