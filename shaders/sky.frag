#version 330 core

in  vec3 TexCoords;
out vec4 FragColor;

uniform float timeOfDay; // 0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset
uniform float time;      // absolute seconds (glfwGetTime) for star twinkling

const float PI = 3.14159265359;

// ── Celestial bodies ────────────────────────────────────────────────────────

// Sun travels east→zenith→west.  Slight Z offset tilts the path off the equator.
vec3 sunDir(float t) {
    float a = (t - 0.25) * 2.0 * PI;
    return normalize(vec3(cos(a), sin(a), 0.25));
}

vec3 moonDir(float t) { return -sunDir(t); } // full moon always opposite sun

// ── Procedural stars ─────────────────────────────────────────────────────────

float hash11(float p) { return fract(sin(p * 127.1) * 43758.5453); }
float hash21(vec2 p)  { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Returns star brightness at `dir`, scaled by `nightness` (0=day, 1=night).
float starField(vec3 dir, float nightness) {
    if (nightness < 0.001 || dir.y < 0.0) return 0.0;

    // Spherical UV — tilted slightly so the seam (u wrap) is below the horizon
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(dir.y, 0.0, 1.0)) / PI;

    vec2  uv   = vec2(u, v) * 160.0; // density of star cells
    vec2  cell = floor(uv);
    vec2  fr   = fract(uv);
    float rng  = hash21(cell);

    if (rng < 0.963) return 0.0; // ~3.7 % of cells contain a star

    vec2  center   = vec2(hash21(cell + vec2(0.31, 0.72)), hash21(cell + vec2(0.74, 0.28)));
    float dist     = length(fr - center);
    float phase    = rng * 628.3;                            // unique phase per star
    float rate     = 1.5 + 2.5 * hash11(rng * 91.7);        // unique twinkle speed
    float twinkle  = 0.72 + 0.28 * sin(time * rate + phase);
    float baseBri  = 0.45 + 0.55 * rng;
    return smoothstep(0.14, 0.0, dist) * baseBri * twinkle * nightness;
}

// ── Disc + corona helper ──────────────────────────────────────────────────────

// Tight disc of angular radius `halfAngleCos` with a soft power-law corona.
float celestialDisc(vec3 dir, vec3 body, float halfAngleCos, float coronaPow) {
    float d      = dot(normalize(dir), normalize(body));
    float disc   = smoothstep(halfAngleCos - 0.0003, halfAngleCos + 0.0003, d);
    float corona = pow(max(d, 0.0), coronaPow) * 0.55;
    return clamp(disc + corona, 0.0, 1.0);
}

// ── Main ─────────────────────────────────────────────────────────────────────

void main() {
    vec3 dir  = normalize(TexCoords);
    vec3 sun  = sunDir(timeOfDay);
    vec3 moon = moonDir(timeOfDay);

    // Sun elevation (-1 = midnight, 0 = horizon, +1 = zenith)
    float sunY    = sun.y;
    float dayness = smoothstep(-0.12, 0.22, sunY); // 0=night, 1=full day

    // Dawn/dusk factor: peaks when sun is near the horizon
    float dawnDusk = smoothstep(-0.30, 0.0, sunY) * (1.0 - smoothstep(0.0, 0.30, sunY));
    // Sunsets read slightly warmer than sunrises (asymmetry adds realism)
    dawnDusk *= (timeOfDay > 0.5) ? 1.25 : 1.0;
    dawnDusk  = clamp(dawnDusk, 0.0, 1.0);

    float nightness = 1.0 - clamp(dayness * 2.2, 0.0, 1.0); // stars fade faster than dayness rises

    // ── Sky gradient (elevation × time-of-day) ──────────────────────────────
    float elev  = clamp(dir.y, 0.0, 1.0);
    float elevP = pow(elev, 0.40); // compress gradient: zenith blue dominates upper half

    vec3 dayZenith     = vec3(0.15, 0.44, 0.92);
    vec3 dayHorizon    = vec3(0.50, 0.75, 0.98);
    vec3 sunsetZenith  = vec3(0.06, 0.12, 0.40);
    vec3 sunsetHorizon = vec3(0.98, 0.40, 0.06);
    vec3 nightZenith   = vec3(0.010, 0.010, 0.055);
    vec3 nightHorizon  = vec3(0.035, 0.045, 0.130);

    vec3 dayColor    = mix(dayHorizon,    dayZenith,    elevP);
    vec3 sunsetColor = mix(sunsetHorizon, sunsetZenith, elevP);
    vec3 nightColor  = mix(nightHorizon,  nightZenith,  elevP);

    vec3 skyColor = mix(nightColor, mix(dayColor, sunsetColor, dawnDusk), dayness);

    // ── Horizon glow in the sun's azimuth at dawn/dusk ──────────────────────
    // Project both the view direction and the sun into the horizontal plane.
    vec2 dirH  = normalize(vec2(dir.x, dir.z) + vec2(0.001)); // avoid zero-length
    vec2 sunH  = normalize(vec2(sun.x, sun.z) + vec2(0.001));
    float azDot = dot(dirH, sunH); // 1 = looking toward sun, -1 = away

    float horizonGlow = pow(max(azDot, 0.0), 5.0) * (1.0 - elevP) * dawnDusk;
    skyColor += horizonGlow * vec3(0.95, 0.38, 0.05) * 0.85;

    // Secondary wider pink scatter band, present even slightly above horizon
    float pinkBand = pow(max(azDot, 0.0), 2.5) * smoothstep(0.15, 0.0, elev) * dawnDusk;
    skyColor += pinkBand * vec3(0.90, 0.25, 0.18) * 0.35;

    // ── Sun disc ─────────────────────────────────────────────────────────────
    float sunVis  = clamp(sunY + 0.06, 0.0, 1.0); // fade when below horizon
    float sunDisc = celestialDisc(dir, sun, 0.9992, 80.0);
    // Sun shifts from deep orange at horizon to pale yellow-white at zenith
    vec3 sunColour = mix(vec3(1.00, 0.48, 0.08), vec3(1.05, 1.00, 0.90),
                         clamp(sunY * 3.5, 0.0, 1.0));
    skyColor = mix(skyColor, sunColour, sunDisc * sunVis);

    // Broad soft glow around sun (scattering halo), visible even through overcast-ish look
    float sunHalo = pow(max(dot(dir, sun), 0.0), 14.0) * sunVis * 0.45;
    skyColor += sunHalo * mix(vec3(0.95, 0.45, 0.10), vec3(0.90, 0.88, 0.75),
                              clamp(sunY * 2.0, 0.0, 1.0));

    // ── Moon disc ─────────────────────────────────────────────────────────────
    float moonVis  = clamp(-sunY + 0.06, 0.0, 1.0);
    float moonDisc = celestialDisc(dir, moon, 0.9994, 220.0);
    skyColor += vec3(0.86, 0.92, 1.00) * moonDisc * moonVis * 0.85;

    // ── Stars ────────────────────────────────────────────────────────────────
    float star = starField(dir, nightness);
    skyColor  += vec3(star * 0.95, star * 0.97, star * 1.00); // slightly cool star tint

    // ── Below-horizon ground cap ──────────────────────────────────────────────
    // Fade to a dark earthy colour below the skyline so looking down isn't jarring.
    if (dir.y < 0.06) {
        vec3 groundCol = mix(vec3(0.10, 0.08, 0.07),
                             mix(nightHorizon, dayHorizon, dayness) * 0.5, 0.25);
        float groundT  = smoothstep(0.06, -0.10, dir.y);
        skyColor = mix(skyColor, groundCol, groundT);
    }

    // Gamma
    skyColor = pow(max(skyColor, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(skyColor, 1.0);
}
