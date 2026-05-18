#version 330 core

in  vec3 TexCoords;
out vec4 FragColor;

uniform float timeOfDay;
uniform float time;

const float PI = 3.14159265359;

// ── Celestial bodies ──────────────────────────────────────────────────────────
vec3 sunDir(float t) {
    float a = (t - 0.25) * 2.0 * PI;
    return normalize(vec3(cos(a), sin(a), 0.25));
}
vec3 moonDir(float t) { return -sunDir(t); }

// ── Noise ─────────────────────────────────────────────────────────────────────
float hash11(float p) { return fract(sin(p * 127.1) * 43758.5453); }
float hash21(vec2 p)  { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float cloudNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i),           hash21(i+vec2(1,0)), f.x),
               mix(hash21(i+vec2(0,1)), hash21(i+vec2(1,1)), f.x), f.y);
}
float cloudFBM(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * cloudNoise(p); p *= 2.0; a *= 0.5; }
    return v;
}

// ── Smooth cloud density — used only for god rays ─────────────────────────────
float smoothCloudDensity(vec2 pos, float t) {
    vec2 s1 = pos * 0.003 + vec2(t * 0.010, t * 0.007);
    vec2 s2 = pos * 0.006 + vec2(-t * 0.016, t * 0.009) + vec2(31.7, 17.3);
    return smoothstep(0.43, 0.72, cloudFBM(s1) * 0.60 + cloudFBM(s2) * 0.40);
}

// ── Voxel cloud constants ─────────────────────────────────────────────────────
const float CLOUD_H     = 80.0;   // height of cloud base (relative units)
const float CLOUD_THICK = 5.0;    // layer thickness
const float CELL_SZ     = 12.0;   // voxel cell size in world units

// Cloud presence for a grid cell: formation scale + cell scale
float cloudVoxelAt(vec2 cell) {
    // Macro formations (~5-cell regions): ~55% of sky has formations
    float macro = hash21(floor(cell / 5.0) + vec2(7.3, 11.7));
    if (macro < 0.45) return 0.0;
    // Per-cell detail: ~62% of cells within a formation are filled
    return step(0.38, hash21(cell + vec2(2.1, 3.7)));
}

// Sample the voxel cloud layer.  Returns cloud face color in .rgb;
// .a == 1.0 means hit, .a == -1.0 means no cloud (signal value).
vec4 voxelCloud(vec3 dir, float t, float dayness, float dawnDusk) {
    if (dir.y < 0.015) return vec4(0.0, 0.0, 0.0, -1.0);

    vec2 scroll = vec2(t * 9.0, t * 1.5);

    // Intersect bottom plane of cloud layer
    vec2 botXZ  = dir.xz / dir.y * CLOUD_H + scroll;
    vec2 cellBot = floor(botXZ / CELL_SZ);

    // Intersect top plane of cloud layer
    vec2 topXZ  = dir.xz / dir.y * (CLOUD_H + CLOUD_THICK) + scroll;
    vec2 cellTop = floor(topXZ / CELL_SZ);

    float botHit = cloudVoxelAt(cellBot);
    float topHit = cloudVoxelAt(cellTop);

    if (botHit < 0.5 && topHit < 0.5) return vec4(0.0, 0.0, 0.0, -1.0);

    // Palette
    vec3 cTop     = mix(vec3(0.98, 0.66, 0.28), vec3(0.96, 0.97, 1.00),
                        clamp(1.0 - dawnDusk * 1.8, 0.0, 1.0));
    vec3 cBot     = vec3(0.55, 0.58, 0.68);          // grey-blue underside
    vec3 cSide    = mix(cBot, cTop, 0.45);            // intermediate side face
    vec3 cNight   = vec3(0.11, 0.12, 0.19);

    // Face selection: both planes hit the same cell → bottom face.
    // Different cells → ray crossed a side boundary → side face.
    bool sameCell = (cellBot == cellTop);
    vec3 dayColor = sameCell ? cBot : cSide;
    // Tint bottom face slightly warm at dawn/dusk
    if (sameCell) dayColor = mix(dayColor * vec3(1.0, 0.82, 0.60), dayColor,
                                  clamp(1.0 - dawnDusk * 1.2, 0.0, 1.0));

    vec3 faceColor = mix(cNight, dayColor, dayness);

    // Fade out very distant clouds (shallow angles → long projection distances)
    float projDist = CLOUD_H / dir.y;
    float fade = clamp(1.0 - (projDist - 400.0) / 350.0, 0.0, 1.0);
    if (fade < 0.001) return vec4(0.0, 0.0, 0.0, -1.0);

    return vec4(faceColor, fade);
}

// ── Stars ─────────────────────────────────────────────────────────────────────
float starField(vec3 dir, float nightness) {
    if (nightness < 0.001 || dir.y < 0.0) return 0.0;
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(dir.y, 0.0, 1.0)) / PI;
    vec2  uv   = vec2(u, v) * 160.0;
    vec2  cell = floor(uv), fr = fract(uv);
    float rng  = hash21(cell);
    if (rng < 0.963) return 0.0;
    vec2  center  = vec2(hash21(cell + vec2(0.31, 0.72)), hash21(cell + vec2(0.74, 0.28)));
    float twinkle = 0.72 + 0.28 * sin(time * (1.5 + 2.5 * hash11(rng * 91.7)) + rng * 628.3);
    return smoothstep(0.14, 0.0, length(fr - center)) * (0.45 + 0.55 * rng) * twinkle * nightness;
}

// ── Celestial disc + corona ────────────────────────────────────────────────────
float celestialDisc(vec3 dir, vec3 body, float halfAngleCos, float coronaPow) {
    float d = dot(normalize(dir), normalize(body));
    return clamp(smoothstep(halfAngleCos - 0.0003, halfAngleCos + 0.0003, d)
                 + pow(max(d, 0.0), coronaPow) * 0.55, 0.0, 1.0);
}

// ── God rays (smooth cloud sampling for soft shafts) ──────────────────────────
float godRays(vec3 dir, vec3 sun, float t, float sunDot, float sunVis) {
    if (sunDot < 0.2 || dir.y < 0.01 || sunVis < 0.01) return 0.0;
    float transmission = 0.0;
    const int STEPS = 8;
    for (int i = 0; i < STEPS; i++) {
        vec3 sd = normalize(mix(dir, sun, float(i) / float(STEPS - 1) * 0.55));
        if (sd.y < 0.01) continue;
        vec2 pos = sd.xz / (sd.y + 0.05) * 200.0;
        transmission += (1.0 - smoothCloudDensity(pos, t)) / float(STEPS);
    }
    return transmission * pow(sunDot, 5.0) * sunVis;
}

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    vec3 dir  = normalize(TexCoords);
    vec3 sun  = sunDir(timeOfDay);
    vec3 moon = moonDir(timeOfDay);

    float sunY    = sun.y;
    float dayness = smoothstep(-0.12, 0.22, sunY);
    float dawnDusk = clamp(
        smoothstep(-0.30, 0.0, sunY) * (1.0 - smoothstep(0.0, 0.30, sunY))
        * ((timeOfDay > 0.5) ? 1.25 : 1.0), 0.0, 1.0);
    float nightness = 1.0 - clamp(dayness * 2.2, 0.0, 1.0);

    // ── Sky gradient ─────────────────────────────────────────────────────────
    float elev  = clamp(dir.y, 0.0, 1.0);
    float elevP = pow(elev, 0.40);

    vec3 dayZenith     = vec3(0.13, 0.34, 0.76);
    vec3 dayHorizon    = vec3(0.50, 0.72, 0.94);
    vec3 sunsetZenith  = vec3(0.05, 0.08, 0.28);
    vec3 sunsetHorizon = vec3(0.94, 0.34, 0.07);
    vec3 nightZenith   = vec3(0.007, 0.009, 0.038);
    vec3 nightHorizon  = vec3(0.025, 0.035, 0.090);

    vec3 dayColor    = mix(dayHorizon,    dayZenith,    elevP);
    vec3 sunsetColor = mix(sunsetHorizon, sunsetZenith, elevP);
    vec3 nightColor  = mix(nightHorizon,  nightZenith,  elevP);
    vec3 skyColor    = mix(nightColor, mix(dayColor, sunsetColor, dawnDusk), dayness);

    // Horizon glow toward sun at dawn/dusk
    vec2 dirH = normalize(dir.xz + vec2(0.001));
    vec2 sunH = normalize(sun.xz + vec2(0.001));
    float azDot = dot(dirH, sunH);
    skyColor += pow(max(azDot, 0.0), 5.0) * (1.0 - elevP) * dawnDusk
                * vec3(0.95, 0.38, 0.05) * 0.85;
    skyColor += pow(max(azDot, 0.0), 2.5) * smoothstep(0.15, 0.0, elev) * dawnDusk
                * vec3(0.90, 0.25, 0.18) * 0.35;

    // ── Sun disc ─────────────────────────────────────────────────────────────
    float sunVis  = clamp(sunY + 0.06, 0.0, 1.0);
    float sunDisc = celestialDisc(dir, sun, 0.9992, 80.0);
    vec3 sunColour = mix(vec3(1.00, 0.48, 0.08), vec3(1.05, 1.00, 0.90),
                         clamp(sunY * 3.5, 0.0, 1.0));
    skyColor = mix(skyColor, sunColour, sunDisc * sunVis);
    skyColor += pow(max(dot(dir, sun), 0.0), 14.0) * sunVis * 0.45
                * mix(vec3(0.95, 0.45, 0.10), vec3(0.90, 0.88, 0.75),
                      clamp(sunY * 2.0, 0.0, 1.0));

    // ── Moon ─────────────────────────────────────────────────────────────────
    float moonVis  = clamp(-sunY + 0.06, 0.0, 1.0);
    skyColor += vec3(0.86, 0.92, 1.00)
                * celestialDisc(dir, moon, 0.9994, 220.0) * moonVis * 0.85;

    // ── Stars ─────────────────────────────────────────────────────────────────
    float star = starField(dir, nightness);
    skyColor  += vec3(star * 0.95, star * 0.97, star * 1.00);

    // ── Voxel clouds ─────────────────────────────────────────────────────────
    vec4 cloud = voxelCloud(dir, time, dayness, dawnDusk);
    if (cloud.a > 0.0) {
        skyColor = mix(skyColor, cloud.rgb, cloud.a);
    }

    // ── God rays (soft, uses smooth cloud density) ────────────────────────────
    float sunDotV = dot(dir, sun);
    float rays    = godRays(dir, sun, time, sunDotV, sunVis);
    skyColor += rays * mix(vec3(0.95, 0.52, 0.14), vec3(0.95, 0.92, 0.80),
                           clamp(sunY * 2.0, 0.0, 1.0)) * dayness * 0.55;

    // ── Below-horizon ground cap ──────────────────────────────────────────────
    if (dir.y < 0.06) {
        vec3 groundCol = mix(vec3(0.10, 0.08, 0.07),
                             mix(nightHorizon, dayHorizon, dayness) * 0.5, 0.25);
        skyColor = mix(skyColor, groundCol, smoothstep(0.06, -0.10, dir.y));
    }

    // Valheim: slight desaturation for naturalistic mood
    float lum = dot(skyColor, vec3(0.299, 0.587, 0.114));
    skyColor = mix(vec3(lum), skyColor, 0.88);

    skyColor = pow(max(skyColor, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(skyColor, 1.0);
}
