#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include <numeric>
#include <random>

// Simple fast Perlin noise
class PerlinNoise {
    std::array<int, 512> p;
public:
    PerlinNoise(unsigned int seed = 0) {
        std::array<int, 256> perm;
        std::iota(perm.begin(), perm.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(perm.begin(), perm.end(), rng);
        for (int i = 0; i < 512; i++) p[i] = perm[i & 255];
    }

    float noise(float x, float y) const {
        int X = (int)floorf(x) & 255;
        int Y = (int)floorf(y) & 255;
        x -= floorf(x); y -= floorf(y);
        float u = fade(x), v = fade(y);
        int a = p[X]+Y, b = p[X+1]+Y;
        return lerp(v, lerp(u, grad(p[a],x,y), grad(p[b],x-1,y)),
                       lerp(u, grad(p[a+1],x,y-1), grad(p[b+1],x-1,y-1)));
    }

    float octave(float x, float y, int octs, float persistence = 0.5f, float lacunarity = 2.0f) const {
        float val = 0, amp = 1, freq = 1, max = 0;
        for (int i = 0; i < octs; i++) {
            val += noise(x * freq, y * freq) * amp;
            max += amp;
            amp *= persistence;
            freq *= lacunarity;
        }
        return val / max;
    }

    float noise(float x, float y, float z) const {
        int X = (int)floorf(x) & 255;
        int Y = (int)floorf(y) & 255;
        int Z = (int)floorf(z) & 255;
        x -= floorf(x); y -= floorf(y); z -= floorf(z);
        float u = fade(x), v = fade(y), w = fade(z);
        int A = p[X]+Y, AA = p[A]+Z, AB = p[A+1]+Z;
        int B = p[X+1]+Y, BA = p[B]+Z, BB = p[B+1]+Z;

        return lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z),
                                     grad(p[BA], x-1, y, z)),
                               lerp(u, grad(p[AB], x, y-1, z),
                                     grad(p[BB], x-1, y-1, z))),
                       lerp(v, lerp(u, grad(p[AA+1], x, y, z-1),
                                     grad(p[BA+1], x-1, y, z-1)),
                               lerp(u, grad(p[AB+1], x, y-1, z-1),
                                     grad(p[BB+1], x-1, y-1, z-1))));
    }

    float octave(float x, float y, float z, int octs, float persistence = 0.5f, float lacunarity = 2.0f) const {
        float val = 0, amp = 1, freq = 1, max = 0;
        for (int i = 0; i < octs; i++) {
            val += noise(x * freq, y * freq, z * freq) * amp;
            max += amp;
            amp *= persistence;
            freq *= lacunarity;
        }
        return val / max;
    }

private:
    float fade(float t) const { return t*t*t*(t*(t*6-15)+10); }
    float lerp(float t, float a, float b) const { return a + t*(b-a); }
    float grad(int hash, float x, float y) const {
        int h = hash & 3;
        float u = h<2 ? x : y, v = h<2 ? y : x;
        return ((h&1)?-u:u) + ((h&2)?-v:v);
    }
    float grad(int hash, float x, float y, float z) const {
        int h = hash & 15;
        float u = h<8 ? x : y, v = h<4 ? y : h==12||h==14 ? x : z;
        return ((h&1)?-u:u) + ((h&2)?-v:v);
    }
};
