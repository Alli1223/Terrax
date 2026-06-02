#include "terrax_test.h"
#include "noise.h"
#include <cmath>

// PerlinNoise (src/noise.h) is the deterministic backbone of world
// generation — terrain height, temperature, humidity, rivers and
// continents are all sampled from seeded instances of it. These tests
// pin down the properties world.cpp relies on: determinism per seed,
// finite bounded output, lattice zeros, and seed sensitivity.

static bool finiteF(float v) { return std::isfinite(v); }

// --- Determinism ---

TEST_CASE(Perlin_2D_DeterministicForSameSeed) {
    PerlinNoise a(7), b(7);
    for (float x = -3.0f; x <= 3.0f; x += 0.7f)
        for (float y = -3.0f; y <= 3.0f; y += 0.7f)
            CHECK(a.noise(x, y) == b.noise(x, y));
}

TEST_CASE(Perlin_3D_DeterministicForSameSeed) {
    PerlinNoise a(42), b(42);
    CHECK(a.noise(1.5f, 2.5f, 3.5f) == b.noise(1.5f, 2.5f, 3.5f));
    CHECK(a.noise(-4.2f, 0.1f, 9.9f) == b.noise(-4.2f, 0.1f, 9.9f));
}

// --- Lattice zeros: classic Perlin returns 0 at integer coordinates ---

TEST_CASE(Perlin_2D_ZeroAtIntegerLattice) {
    PerlinNoise n(123);
    for (int x = -5; x <= 5; x++)
        for (int y = -5; y <= 5; y++)
            CHECK(std::fabs(n.noise((float)x, (float)y)) < 1e-5f);
}

// --- Output is finite and bounded ---

TEST_CASE(Perlin_2D_FiniteAndBounded) {
    PerlinNoise n(2024);
    for (float x = -20.0f; x <= 20.0f; x += 1.3f)
        for (float y = -20.0f; y <= 20.0f; y += 1.3f) {
            float v = n.noise(x, y);
            CHECK(finiteF(v));
            CHECK(std::fabs(v) <= 1.5f);
        }
}

TEST_CASE(Perlin_3D_FiniteAndBounded) {
    PerlinNoise n(2025);
    for (float x = -8.0f; x <= 8.0f; x += 1.7f)
        for (float y = -8.0f; y <= 8.0f; y += 1.7f) {
            float v = n.noise(x, y, x * 0.5f - y);
            CHECK(finiteF(v));
            CHECK(std::fabs(v) <= 1.5f);
        }
}

// --- The field actually varies (a constant field would be a bug) ---

TEST_CASE(Perlin_2D_FieldVaries) {
    PerlinNoise n(99);
    float first = n.noise(0.5f, 0.5f);
    bool varied = false;
    for (float x = 0.1f; x <= 10.0f && !varied; x += 0.31f)
        for (float y = 0.1f; y <= 10.0f && !varied; y += 0.31f)
            if (std::fabs(n.noise(x, y) - first) > 1e-3f) varied = true;
    CHECK(varied);
}

// --- octave() averages to a finite, bounded value and varies ---

TEST_CASE(Perlin_Octave_FiniteAndBounded) {
    PerlinNoise n(7);
    for (float x = -5.0f; x <= 5.0f; x += 1.1f) {
        float v = n.octave(x, x * 0.3f, 5, 0.5f, 2.0f);
        CHECK(finiteF(v));
        CHECK(std::fabs(v) <= 1.5f);
    }
}

TEST_CASE(Perlin_Octave3D_FiniteAndBounded) {
    PerlinNoise n(8);
    float v = n.octave(1.2f, 3.4f, 5.6f, 4, 0.5f, 2.0f);
    CHECK(finiteF(v));
    CHECK(std::fabs(v) <= 1.5f);
}

// --- Different seeds produce different fields ---

TEST_CASE(Perlin_DifferentSeedsDiffer) {
    PerlinNoise a(1), b(2);
    bool differ = false;
    for (float x = 0.13f; x <= 6.0f && !differ; x += 0.37f)
        for (float y = 0.13f; y <= 6.0f && !differ; y += 0.37f)
            if (std::fabs(a.noise(x, y) - b.noise(x, y)) > 1e-4f) differ = true;
    CHECK(differ);
}
