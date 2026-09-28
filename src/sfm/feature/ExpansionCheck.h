// Pair expansion's consistency test: a candidate (a, b) reached from a verified
// seed through a neighbour pair closes a triangle, and a real match's rotation
// agrees with the other two sides composed. On a 1872-frame walk 57% of the
// pairs expansion verified contradicted the reconstruction; repeated structure
// passes two-view RANSAC but almost never closes a triangle (src/sfm/README.md).
#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sfm/geometry/LinAlg.h"
#include "sfm/geometry/TwoView.h"

namespace sfm {

// Camera-1 -> camera-2 rotation from a verified pair's inliers (`max_error` in
// radians): the pure-rotation fit where it explains 80% of them (the essential
// matrix is ill-posed there: H pairs, near-zero baselines), else the essential's.
inline bool pairRotation(const std::vector<Vec3>& b1, const std::vector<Vec3>& b2,
                         double max_error, Mat3& R) {
    const size_t n = b1.size();
    if (n < 5 || b2.size() != n) return false;
    Mat3 acc{};
    for (size_t k = 0; k < n; k++) {
        const double u[3] = {b2[k].x, b2[k].y, b2[k].z}, v[3] = {b1[k].x, b1[k].y, b1[k].z};
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) acc[3 * r + c] += u[r] * v[c];
    }
    const Svd3 s = svd3(acc);
    Mat3 D = mat3Identity();
    D[8] = det3(s.U) * det3(s.V) < 0 ? -1.0 : 1.0;
    const Mat3 Rrot = mul(mul(s.U, D), transpose(s.V));
    size_t explained = 0;
    const double tol = 3.0 * max_error;
    for (size_t k = 0; k < n; k++) {
        const double c = std::max(-1.0, std::min(1.0, mul(Rrot, b1[k]).dot(b2[k])));
        if (std::acos(c) < tol) explained++;
    }
    if (explained >= 0.8 * (double)n) {
        R = Rrot;
        return true;
    }
    TwoViewOptions opt;
    opt.ransac.max_error = max_error;
    opt.min_num_inliers = 5;
    opt.estimate_homography = false;
    opt.recover_pose = true;
    const TwoViewGeometry g = estimateTwoViewBearing(b1, b2, opt);
    R = g.has_pose ? g.pose.R : Rrot;
    return true;
}

// Degrees between two rotations.
inline double rotationDistanceDeg(const Mat3& A, const Mat3& B) {
    const Mat3 D = mul(A, transpose(B));
    const double c = std::max(-1.0, std::min(1.0, (D[0] + D[4] + D[8] - 1.0) * 0.5));
    return std::acos(c) * 180.0 / M_PI;
}

// Rotations of verified pairs, each stored once as lower image -> higher.
class PairRotations {
public:
    static uint64_t key(uint32_t a, uint32_t b) {
        return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    }
    void set(uint32_t a, uint32_t b, const Mat3& R_ab) {
        rot_[key(a, b)] = a < b ? R_ab : transpose(R_ab);
    }
    bool has(uint32_t a, uint32_t b) const { return rot_.count(key(a, b)) != 0; }
    // a -> b, whichever order the pair was stored in.
    bool get(uint32_t a, uint32_t b, Mat3& R) const {
        const auto it = rot_.find(key(a, b));
        if (it == rot_.end()) return false;
        R = a < b ? it->second : transpose(it->second);
        return true;
    }

private:
    std::unordered_map<uint64_t, Mat3> rot_;
};

enum class TriangleVerdict { Consistent, Inconsistent, Uncheckable };

// One triangle that reached candidate (a, b): `moved` is the candidate end one
// image away from the seed's, `k` the seed's end there, `fixed` the shared one.
// Its sides are the neighbour pair (moved, k) and the seed (k, fixed).
struct ExpansionTriangle {
    uint32_t moved, k, fixed;
};

inline std::vector<ExpansionTriangle> expansionTriangles(
    uint32_t a, uint32_t b, const std::vector<uint32_t>& run,
    const std::unordered_set<uint64_t>& seeds) {
    std::vector<ExpansionTriangle> out;
    const uint32_t n = (uint32_t)run.size();
    const uint32_t ends[2] = {a, b}, others[2] = {b, a};
    for (int side = 0; side < 2; side++) {
        const uint32_t moved = ends[side], fixed = others[side];
        if (moved >= n) continue;
        for (int step : {-1, 1}) {
            const int64_t k64 = (int64_t)moved + step;
            if (k64 < 0 || k64 >= n) continue;
            const uint32_t k = (uint32_t)k64;
            if (run[k] != run[moved] || k == fixed) continue;
            if (seeds.count(PairRotations::key(k, fixed))) out.push_back({moved, k, fixed});
        }
    }
    return out;
}

// Candidate (a, b) against the triangles that reached it: one within `max_deg`
// is enough, and none whose sides all have rotations is its own answer.
inline TriangleVerdict checkTriangles(uint32_t a, uint32_t b, const std::vector<uint32_t>& run,
                                      const std::unordered_set<uint64_t>& seeds,
                                      const PairRotations& rot, double max_deg) {
    Mat3 R_ab;
    if (!rot.get(a, b, R_ab)) return TriangleVerdict::Uncheckable;
    bool checked = false;
    for (const ExpansionTriangle& t : expansionTriangles(a, b, run, seeds)) {
        Mat3 R_mk, R_kf;  // moved -> k, k -> fixed
        if (!rot.get(t.moved, t.k, R_mk) || !rot.get(t.k, t.fixed, R_kf)) continue;
        const Mat3 through = mul(R_kf, R_mk);  // moved -> fixed
        const Mat3 pred = t.moved == a ? through : transpose(through);
        checked = true;
        if (rotationDistanceDeg(pred, R_ab) <= max_deg) return TriangleVerdict::Consistent;
    }
    return checked ? TriangleVerdict::Inconsistent : TriangleVerdict::Uncheckable;
}

}  // namespace sfm
