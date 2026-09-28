// The dense Cholesky of CpuDense.h through the platform LAPACK, where the build
// has one (Apple's Accelerate). On any other build these report unavailable and
// CpuDense.h keeps its own factorization. See README.md, "Host fallback".
#pragma once

#include <cstdint>

namespace bacpu {

// Built with a LAPACK and not turned off by SS_SFM_BA_LAPACK=0.
bool lapackEnabled();

// Factor the packed lower triangle `a` (row r at r(r+1)/2) in place, with `rfp`
// (n(n+1)/2 doubles) as scratch. False, `a` untouched, on failure or a pivot the
// guard in CpuDense.h would replace -- `diag`/`rel` as DenseSpd::factor takes them.
bool lapackFactor(double* a, double* rfp, uint32_t n, const double* diag, double rel);

}  // namespace bacpu
