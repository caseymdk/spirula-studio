// Pair expansion (sfm/feature/Pairing.h expansionPairs): the neighbours one
// verified pair leads to, the rounds run over a synthetic overlap oracle the
// way matchFeatureDir runs them, and the rotation test (ExpansionCheck.h).
//
// Prints PASS/FAIL and returns 0/1. See docs/testing.md.
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sfm/feature/ExpansionCheck.h"
#include "sfm/feature/Pairing.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;
using Pair = std::pair<uint32_t, uint32_t>;

static int fails = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

static uint64_t key(const Pair& p) { return ((uint64_t)p.first << 32) | p.second; }

static void testNeighbours() {
    const std::vector<uint32_t> run(20, 0);
    std::unordered_set<uint64_t> tried;
    check(expansionPairs({{10, 15}}, run, tried) ==
              std::vector<Pair>({{9, 15}, {10, 14}, {10, 16}, {11, 15}}),
          "10+15 leads to 9+15, 10+14, 10+16 and 11+15");
    // (4,4) and (5,5) are not pairs.
    check(expansionPairs({{4, 5}}, run, tried) == std::vector<Pair>({{3, 5}, {4, 6}}),
          "4+5 leads to 3+5 and 4+6");
    check(expansionPairs({{4, 5}, {5, 6}}, run, tried) ==
              std::vector<Pair>({{3, 5}, {4, 6}, {5, 7}}),
          "a shared neighbour is listed once");
    tried.insert(key({3, 5}));
    check(expansionPairs({{4, 5}}, run, tried) == std::vector<Pair>({{4, 6}}),
          "a pair already tried is not offered again");
    check(expansionPairs({{0, 19}}, run, {}) == std::vector<Pair>({{0, 18}, {1, 19}}),
          "the ends of the list have one neighbour each");
}

static void testFolders() {
    // cam0 is images 0-4, cam1 is 5-9: 4 and 5 are neighbours by index only.
    std::vector<std::string> names;
    for (int i = 0; i < 5; i++) names.push_back("cam0/" + std::to_string(i));
    for (int i = 0; i < 5; i++) names.push_back("cam1/" + std::to_string(i));
    const std::vector<uint32_t> run = folderRuns(names);
    check(expansionPairs({{4, 9}}, run, {}) == std::vector<Pair>({{3, 9}, {4, 8}}),
          "no neighbour across a folder boundary");
    check(expansionPairs({{2, 5}}, run, {}) ==
              std::vector<Pair>({{1, 5}, {2, 6}, {3, 5}}),
          "a cross-folder pair expands inside each folder");
}

// Images i and j overlap when |i - j| <= 3, so the band around the diagonal is
// everything there is to find. A seed deep inside it has to grow to the whole
// band and stop at its edge, never trying a pair more than once.
static void testRounds() {
    const uint32_t n = 40;
    const std::vector<uint32_t> run(n, 0);
    auto overlaps = [](const Pair& p) { return p.second - p.first <= 3; };
    std::unordered_set<uint64_t> tried;
    std::set<Pair> found;
    std::vector<Pair> seeds = {{20, 22}};
    tried.insert(key(seeds[0]));
    found.insert(seeds[0]);
    int rounds = 0;
    size_t offered = 0;
    while (!seeds.empty()) {
        const std::vector<Pair> cand = expansionPairs(seeds, run, tried);
        if (cand.empty()) break;
        rounds++;
        std::vector<Pair> next;
        for (const Pair& q : cand) {
            check(tried.insert(key(q)).second, "a candidate was offered twice");
            offered++;
            if (overlaps(q)) {
                found.insert(q);
                next.push_back(q);
            }
        }
        seeds.swap(next);
    }
    size_t band = 0;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i + 1; j < n && j - i <= 3; j++) band++;
    check(found.size() == band, "every overlapping pair was found");
    // The only misses are the pairs just outside the band: one step past its
    // edge, and never further, since a miss seeds nothing.
    for (uint64_t k : tried) {
        const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)k;
        if (b - a > 4) {
            check(false, "a pair two steps past the band was tried");
            break;
        }
    }
    printf("  band %zu pairs found in %d rounds, %zu offered\n", band, rounds, offered);
    check(rounds > 1, "the band takes more than one round");
}

static Mat3 rotAxis(double ax, double ay, double az) {
    const double th = std::sqrt(ax * ax + ay * ay + az * az);
    if (th < 1e-12) return mat3Identity();
    const double x = ax / th, y = ay / th, z = az / th, c = std::cos(th), s = std::sin(th),
                 C = 1 - c;
    return {c + x * x * C,     x * y * C - z * s, x * z * C + y * s,
            y * x * C + z * s, c + y * y * C,     y * z * C - x * s,
            z * x * C - y * s, z * y * C + x * s, c + z * z * C};
}

// Bearings of `pts` seen by a camera at centre C with world -> camera rotation R.
static std::vector<Vec3> view(const std::vector<Vec3>& pts, const Mat3& R, const Vec3& C) {
    std::vector<Vec3> out;
    for (const Vec3& X : pts) out.push_back(mul(R, X - C).normalized());
    return out;
}

static void testRotation() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<Vec3> pts;
    for (int k = 0; k < 200; k++) pts.push_back({u(rng) * 3, u(rng) * 2, 6 + u(rng) * 2});
    const Mat3 Ra = rotAxis(0.02, -0.05, 0.01), Rb = rotAxis(-0.1, 0.25, 0.05);
    const Mat3 truth = mul(Rb, transpose(Ra));  // a -> b
    Mat3 R;
    // A real baseline: the essential matrix's rotation.
    check(pairRotation(view(pts, Ra, {0, 0, 0}), view(pts, Rb, {1.2, 0.1, 0.3}), 1e-3, R) &&
              rotationDistanceDeg(R, truth) < 0.5,
          "rotation of a pair with a baseline");
    // No baseline: the pure-rotation fit.
    check(pairRotation(view(pts, Ra, {0, 0, 0}), view(pts, Rb, {0, 0, 0}), 1e-3, R) &&
              rotationDistanceDeg(R, truth) < 0.1,
          "rotation of a pure-rotation pair");
}

static void testTriangles() {
    // Four frames turning 8 degrees apart; seed (1, 3) reaches candidate (0, 3)
    // through the neighbour pair (0, 1).
    const std::vector<uint32_t> run(4, 0);
    std::vector<Mat3> R;
    for (int i = 0; i < 4; i++) R.push_back(rotAxis(0, 0.14 * i, 0));
    auto rel = [&](int a, int b) { return mul(R[b], transpose(R[a])); };
    PairRotations rot;
    rot.set(0, 1, rel(0, 1));
    rot.set(1, 3, rel(1, 3));
    rot.set(0, 3, rel(0, 3));
    const std::unordered_set<uint64_t> seeds = {PairRotations::key(1, 3)};
    check(checkTriangles(0, 3, run, seeds, rot, 5.0) == TriangleVerdict::Consistent,
          "a true candidate closes its triangle");
    rot.set(0, 3, mul(rotAxis(0.3, 0, 0), rel(0, 3)));  // a match to the wrong place
    check(checkTriangles(0, 3, run, seeds, rot, 5.0) == TriangleVerdict::Inconsistent,
          "a candidate 17 degrees off is rejected");
    // Candidate (1, 2) from seed (1, 3) closes through (2, 3), which has no rotation.
    rot.set(1, 2, rel(1, 2));
    check(checkTriangles(1, 2, run, seeds, rot, 5.0) == TriangleVerdict::Uncheckable,
          "a triangle with a side unmeasured cannot vouch for a candidate");
    rot.set(2, 3, rel(2, 3));
    check(checkTriangles(1, 2, run, seeds, rot, 5.0) == TriangleVerdict::Consistent,
          "the triangle closes through the second image too");
}

static int cmdPairExpansionTest(int, char**) {
    testNeighbours();
    testFolders();
    testRounds();
    testRotation();
    testTriangles();
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    return sfmTestMain(argc - 1, argv + 1, cmdPairExpansionTest);
}
