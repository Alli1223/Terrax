// Intra-town paths and inter-town highways (with docks, bridges and ferry
// links), plus street-lamp placement — split out of town.cpp. See town_internal.h.
#include "town_internal.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace townint {

// --- Intra-town paths --------------------------------------------------------

// Carves a gravel path from every house to the village well, routed with A*
// around the other buildings so paths bend instead of cutting through walls.
void routeTownPaths(Town& t) {
    if (t.buildings.size() < 2) return;
    const int CELL = 2, MARGIN = 14;
    int minx = t.bbMin.x - MARGIN, minz = t.bbMin.y - MARGIN;
    int maxx = t.bbMax.x + MARGIN, maxz = t.bbMax.y + MARGIN;
    int gw = (maxx - minx) / CELL + 1;
    int gh = (maxz - minz) / CELL + 1;
    if (gw < 2 || gh < 2) return;

    auto w2c = [&](int wx, int wz) {
        int cx = (wx - minx) / CELL, cz = (wz - minz) / CELL;
        return glm::ivec2(std::min(gw - 1, std::max(0, cx)),
                          std::min(gh - 1, std::max(0, cz)));
    };
    auto c2w = [&](int cx, int cz) {
        return glm::ivec2(minx + cx * CELL + CELL / 2, minz + cz * CELL + CELL / 2);
    };

    // Obstacle grid: -1 free, otherwise the index of the building occupying it.
    std::vector<int> obst((size_t)gw * gh, -1);
    for (size_t bi = 0; bi < t.buildings.size(); bi++) {
        const TownBuilding& b = t.buildings[bi];
        if (b.kind == 0) continue;                          // the well stays walkable
        glm::ivec2 lo = w2c(b.wx - 1, b.wz - 1);
        glm::ivec2 hi = w2c(b.wx + b.dimX + 1, b.wz + b.dimZ + 1);
        for (int cz = lo.y; cz <= hi.y; cz++)
            for (int cx = lo.x; cx <= hi.x; cx++)
                obst[(size_t)cz * gw + cx] = (int)bi;
    }

    glm::ivec2 wellC = w2c(t.center.x, t.center.y);

    for (size_t bi = 0; bi < t.buildings.size(); bi++) {
        const TownBuilding& b = t.buildings[bi];
        // Paths originate at every building that has rooms (houses, pubs,
        // blacksmiths, mage towers) — centrepieces and farms are skipped.
        if (b.rooms.empty()) continue;
        int hcx = b.wx + b.dimX / 2, hcz = b.wz + b.dimZ / 2;
        glm::ivec2 hC = w2c(hcx, hcz);
        int self = (int)bi;
        auto cost = [&obst, gw, self](int cx, int cz) -> float {
            int o = obst[(size_t)cz * gw + cx];
            return (o < 0 || o == self) ? 1.0f : -1.0f;     // own footprint is passable
        };
        std::vector<glm::ivec2> cells = astar(gw, gh, hC, wellC, cost, 1.0f, 60000);

        TownRoad p;
        if (cells.size() >= 2) {
            for (const glm::ivec2& cc : cells)
                p.pts.push_back(c2w(cc.x, cc.y));
        } else {                                            // fallback: straight line
            p.pts.push_back(glm::ivec2(hcx, hcz));
            p.pts.push_back(t.center);
        }
        t.paths.push_back(std::move(p));
    }

    // Grow the town bounding box to cover routed paths (used for stamp culling).
    for (const TownRoad& p : t.paths)
        for (const glm::ivec2& pt : p.pts) {
            t.bbMin.x = std::min(t.bbMin.x, pt.x);
            t.bbMin.y = std::min(t.bbMin.y, pt.y);
            t.bbMax.x = std::max(t.bbMax.x, pt.x);
            t.bbMax.y = std::max(t.bbMax.y, pt.y);
        }
}

// --- Highways & docks --------------------------------------------------------

// Places (or reuses) a dock where a highway meets the water. `landW` is a route
// point on land and `waterW` the next route point out over the water. Returns
// the world-XZ block the jetty roots at.
glm::ivec2 placeDock(TownPlan& plan, glm::ivec2 landW, glm::ivec2 waterW) {
    int sx = (waterW.x > landW.x) - (waterW.x < landW.x);
    int sz = (waterW.y > landW.y) - (waterW.y < landW.y);
    if (sx != 0 && sz != 0) {                               // force a single cardinal axis
        if (std::abs(waterW.x - landW.x) >= std::abs(waterW.y - landW.y)) sz = 0;
        else                                                              sx = 0;
    }
    if (sx == 0 && sz == 0) sx = 1;

    // March from the land point toward the water; stop on the last solid block.
    int x = landW.x, z = landW.y;
    for (int step = 0; step < 256; step++) {
        if (sampleSurface(x + sx, z + sz).height < WORLD_SEA_LEVEL) break;
        x += sx; z += sz;
    }

    for (const TownDock& o : plan.docks)                    // reuse a nearby jetty
        if (std::abs(o.root.x - x) + std::abs(o.root.y - z) < 14)
            return o.root;

    // No jetties inside any settlement — coastal towns included. A dock stamped
    // among the houses clutters the waterfront, so the road still meets the
    // shore here but grows no jetty; any ferry link to this point is dropped
    // because it won't resolve to a dock (see ferry_routes.cpp).
    for (const Town& t : plan.towns) {
        long long dx = (long long)x - t.center.x;
        long long dz = (long long)z - t.center.y;
        long long r  = (long long)t.radius + 90;
        if (dx * dx + dz * dz < r * r)
            return glm::ivec2(x, z);                        // road still meets the shore
    }

    TownDock dk;
    dk.root = glm::ivec2(x, z);
    dk.dx = sx; dk.dz = sz;
    plan.docks.push_back(dk);
    return dk.root;
}

// Resamples a world-XZ polyline to roughly even `spacing`-block steps.
std::vector<glm::ivec2> densifyPath(const std::vector<glm::ivec2>& poly, int spacing) {
    std::vector<glm::ivec2> out;
    if (poly.empty()) return out;
    out.push_back(poly[0]);
    for (size_t i = 0; i + 1 < poly.size(); i++) {
        int ax = poly[i].x, az = poly[i].y;
        int dx = poly[i + 1].x - ax, dz = poly[i + 1].y - az;
        int len   = (int)std::sqrt((double)((long long)dx * dx + (long long)dz * dz));
        int steps = std::max(1, len / std::max(1, spacing));
        for (int s = 1; s <= steps; s++)
            out.push_back(glm::ivec2(ax + dx * s / steps, az + dz * s / steps));
    }
    return out;
}

// Fine A* from a town centre out through its own buildings to a portal point
// clear of the settlement, so a highway weaves past the houses before going
// direct. Returns a world-XZ polyline (centre first, portal last).
std::vector<glm::ivec2> routeTownExit(const Town& t, glm::ivec2 portalW) {
    const int CELL = 2, MARGIN = 12;
    int minx = std::min(portalW.x, t.bbMin.x) - MARGIN;
    int minz = std::min(portalW.y, t.bbMin.y) - MARGIN;
    int maxx = std::max(portalW.x, t.bbMax.x) + MARGIN;
    int maxz = std::max(portalW.y, t.bbMax.y) + MARGIN;
    int gw = (maxx - minx) / CELL + 1;
    int gh = (maxz - minz) / CELL + 1;
    if (gw < 2 || gh < 2) return { t.center, portalW };

    auto w2c = [&](int wx, int wz) {
        int cx = (wx - minx) / CELL, cz = (wz - minz) / CELL;
        return glm::ivec2(std::min(gw - 1, std::max(0, cx)),
                          std::min(gh - 1, std::max(0, cz)));
    };
    auto c2w = [&](int cx, int cz) {
        return glm::ivec2(minx + cx * CELL + CELL / 2, minz + cz * CELL + CELL / 2);
    };

    std::vector<char> blocked((size_t)gw * gh, 0);
    for (const TownBuilding& b : t.buildings) {
        if (b.kind == 0) continue;                          // the well is passable
        glm::ivec2 lo = w2c(b.wx - 1, b.wz - 1);
        glm::ivec2 hi = w2c(b.wx + b.dimX + 1, b.wz + b.dimZ + 1);
        for (int cz = lo.y; cz <= hi.y; cz++)
            for (int cx = lo.x; cx <= hi.x; cx++)
                blocked[(size_t)cz * gw + cx] = 1;
    }

    glm::ivec2 sc = w2c(t.center.x, t.center.y);
    glm::ivec2 gc = w2c(portalW.x, portalW.y);
    blocked[(size_t)sc.y * gw + sc.x] = 0;
    for (int dz = -1; dz <= 1; dz++)                        // keep the portal reachable
        for (int dx = -1; dx <= 1; dx++) {
            int cx = gc.x + dx, cz = gc.y + dz;
            if (cx >= 0 && cx < gw && cz >= 0 && cz < gh)
                blocked[(size_t)cz * gw + cx] = 0;
        }

    auto cost = [&blocked, gw](int cx, int cz) -> float {
        return blocked[(size_t)cz * gw + cx] ? -1.0f : 1.0f;
    };
    std::vector<glm::ivec2> cells = astar(gw, gh, sc, gc, cost, 1.0f, 80000);
    if (cells.size() < 2) return { t.center, portalW };

    std::vector<glm::ivec2> out;
    out.push_back(t.center);
    for (size_t ci = 1; ci + 1 < cells.size(); ci++)
        out.push_back(c2w(cells[ci].x, cells[ci].y));
    out.push_back(portalW);
    return out;
}

// Turns a full highway route into stamped features: flat bridges over gullies
// or rivers up to 200 blocks wide, gravel road on open ground, and a dock on
// each shore where the route meets water (the water span is left clear).
void emitHighwayRoute(TownPlan& plan, const std::vector<glm::ivec2>& route) {
    const int SP = 4;
    std::vector<glm::ivec2> P = densifyPath(route, SP);
    const int n = (int)P.size();
    if (n < 2) return;

    std::vector<int> H(n);
    for (int i = 0; i < n; i++) H[i] = sampleSurface(P[i].x, P[i].y).height;

    // Route points within a town — a bridge must never span into one (towns
    // are flattened, so any crossing there is just flat road).
    std::vector<char> inTown(n, 0);
    for (int i = 0; i < n; i++)
        for (const Town& t : plan.towns) {
            long long dx = (long long)P[i].x - t.center.x;
            long long dz = (long long)P[i].y - t.center.y;
            long long rr = (long long)t.radius + 90;   // clear the whole flattened footprint
            if (dx * dx + dz * dz < rr * rr) { inTown[i] = 1; break; }
        }

    const int MAXSPAN = 200 / SP;   // longest bridge span — 200 blocks
    const int STEEP   = 48  / SP;   // the drop must occur within 48 blocks
    const int MINDROP = 14;         // and fall at least 14 blocks below the rim

    // Detect bridge spans: a steep drop of at least 14 blocks that recovers to
    // a similar level within 200 blocks — a genuine ravine, not a gentle dip.
    struct Span { int s, e, deckY; };
    std::vector<Span> spans;
    {
        int i = 0;
        while (i < n - 1) {
            if (inTown[i]) { i++; continue; }               // never start a span in a town
            int h0  = H[i];
            int lim = std::min(n - 1, i + STEEP);
            int j   = i + 1;
            while (j <= lim && H[j] > h0 - MINDROP) j++;
            if (j > lim) { i++; continue; }
            int s = i;                                      // rim = highest point pre-dip
            for (int q = i; q <= j; q++) if (H[q] > H[s]) s = q;
            if (inTown[s]) { i++; continue; }
            int deckY = H[s];
            int lim2  = std::min(n - 1, s + MAXSPAN);
            int k     = j + 1;
            while (k <= lim2 && H[k] < deckY - 2 && !inTown[k]) k++;  // stop at the town edge
            if (k <= lim2 && H[k] >= deckY - 2) {
                spans.push_back({ s, k, deckY });
                i = k;
            } else {
                i++;
            }
        }
    }

    // Emits gravel road for an index range, split at water (a dock per shore).
    auto emitRoad = [&](int a, int b) {
        TownRoad   cur;
        bool       inWater  = false;
        glm::ivec2 lastLand = P[a];
        int        waterEnter = 0;
        glm::ivec2 dockEnter(0);
        for (int idx = a; idx <= b; idx++) {
            if (H[idx] >= WORLD_SEA_LEVEL) {
                if (inWater) {                              // water -> land
                    glm::ivec2 dockExit = placeDock(plan, P[idx], P[idx - 1]);
                    cur = TownRoad();
                    cur.pts.push_back(dockExit);
                    inWater = false;
                    // A crossing wider than 200 blocks is served by a ferry.
                    if ((idx - waterEnter) * SP > 200)
                        plan.ferryLinks.push_back({ dockEnter, dockExit });
                }
                cur.pts.push_back(P[idx]);
                lastLand = P[idx];
            } else if (!inWater) {                          // land -> water
                dockEnter = placeDock(plan, lastLand, P[idx]);
                cur.pts.push_back(dockEnter);
                if (cur.pts.size() >= 2) plan.highways.push_back(std::move(cur));
                cur = TownRoad();
                inWater = true;
                waterEnter = idx;
            }
        }
        if (!inWater && cur.pts.size() >= 2) plan.highways.push_back(std::move(cur));
    };

    int roadStart = 0;
    for (const Span& sp : spans) {
        emitRoad(roadStart, sp.s);
        TownBridge tb;
        tb.deckY = sp.deckY;
        for (int idx = sp.s; idx <= sp.e; idx++) tb.pts.push_back(P[idx]);
        plan.bridges.push_back(std::move(tb));
        roadStart = sp.e;
    }
    emitRoad(roadStart, n - 1);
}

// Routes a highway between every settlement and its two nearest neighbours.
// Each link weaves out of its endpoint towns with a fine A* pass, then takes a
// direct, terrain-following A* route across the coarse survey grid.
void routeHighways(TownPlan& plan, const std::vector<int16_t>& hgt) {
    const int N = (int)plan.towns.size();
    if (N < 2) return;

    // Survey-cell penalty so the trunk route bends around other settlements.
    std::vector<float> townPen((size_t)GRID * GRID, 0.0f);
    for (const Town& t : plan.towns) {
        int r = t.radius + 40;
        int c0x = worldToCell(t.center.x - r), c1x = worldToCell(t.center.x + r);
        int c0z = worldToCell(t.center.y - r), c1z = worldToCell(t.center.y + r);
        for (int gz = c0z; gz <= c1z; gz++)
            for (int gx = c0x; gx <= c1x; gx++) {
                long long dx = cellWorld(gx) - t.center.x;
                long long dz = cellWorld(gz) - t.center.y;
                if (dx * dx + dz * dz < (long long)r * r)
                    townPen[(size_t)gz * GRID + gx] = 28.0f;
            }
    }

    auto highwayCost = [&hgt, &townPen](int gx, int gz) -> float {
        size_t k = (size_t)gz * GRID + gx;
        int h = (int)hgt[k];
        if (h < WORLD_SEA_LEVEL) return 22.0f + townPen[k];  // open water
        int rough = 0;                                       // local steepness
        if (gx > 0)        rough = std::max(rough, std::abs(h - (int)hgt[k - 1]));
        if (gx < GRID - 1) rough = std::max(rough, std::abs(h - (int)hgt[k + 1]));
        if (gz > 0)        rough = std::max(rough, std::abs(h - (int)hgt[k - GRID]));
        if (gz < GRID - 1) rough = std::max(rough, std::abs(h - (int)hgt[k + GRID]));
        return 1.0f + (float)rough * 0.20f + townPen[k];
    };

    // Link every settlement to its two nearest neighbours (deduplicated).
    std::set<std::pair<int, int>> edges;
    std::vector<std::pair<long long, int>> d;
    for (int i = 0; i < N; i++) {
        d.clear();
        for (int j = 0; j < N; j++) {
            if (j == i) continue;
            long long dx = plan.towns[i].center.x - plan.towns[j].center.x;
            long long dz = plan.towns[i].center.y - plan.towns[j].center.y;
            d.push_back({ dx * dx + dz * dz, j });
        }
        int take = std::min(2, (int)d.size());
        std::partial_sort(d.begin(), d.begin() + take, d.end());
        for (int k = 0; k < take; k++)
            edges.insert({ std::min(i, d[k].second), std::max(i, d[k].second) });
    }

    // A point clear of town `t`, ~radius+50 out toward `toward`, pulled to land.
    auto portalOf = [](const Town& t, glm::ivec2 toward) {
        float dx = (float)(toward.x - t.center.x);
        float dz = (float)(toward.y - t.center.y);
        float L  = std::sqrt(dx * dx + dz * dz);
        if (L < 1.0f) { dx = 1.0f; dz = 0.0f; L = 1.0f; }
        glm::ivec2 p = t.center;
        for (int reach = t.radius + 50; reach > t.radius; reach -= 6) {
            p.x = t.center.x + (int)(dx / L * reach);
            p.y = t.center.y + (int)(dz / L * reach);
            if (sampleSurface(p.x, p.y).height >= WORLD_SEA_LEVEL) break;
        }
        return p;
    };

    // Bearing, measured from `c`, at which polyline `pts` first reaches radius
    // `R` (optionally scanning from the far end). Used to seat a wall gate where
    // the highway actually crosses the wall — the road weaves out of town, so
    // that point is offset from the straight-line bearing to the neighbour.
    auto crossingAngle = [](const std::vector<glm::ivec2>& pts, glm::ivec2 c,
                            float R, bool fromEnd) -> float {
        int n = (int)pts.size();
        glm::vec2 cf((float)c.x, (float)c.y), prev = cf;
        for (int k = 0; k < n; k++) {
            int i = fromEnd ? (n - 1 - k) : k;
            glm::vec2 p((float)pts[i].x, (float)pts[i].y);
            float d = glm::length(p - cf);
            if (d >= R) {
                float dp = glm::length(prev - cf);
                float t  = (R - dp) / std::max(0.001f, d - dp);
                glm::vec2 x = prev + (p - prev) * glm::clamp(t, 0.0f, 1.0f);
                return std::atan2(x.y - cf.y, x.x - cf.x);
            }
            prev = p;
        }
        glm::vec2 last((float)pts[fromEnd ? 0 : n - 1].x,
                       (float)pts[fromEnd ? 0 : n - 1].y);
        return std::atan2(last.y - cf.y, last.x - cf.x);
    };

    for (const auto& e : edges) {
        const Town& tA = plan.towns[e.first];
        const Town& tB = plan.towns[e.second];
        glm::ivec2 portA = portalOf(tA, tB.center);
        glm::ivec2 portB = portalOf(tB, tA.center);

        glm::ivec2 sc(worldToCell(portA.x), worldToCell(portA.y));
        glm::ivec2 gc(worldToCell(portB.x), worldToCell(portB.y));
        std::vector<glm::ivec2> trunk =
            astar(GRID, GRID, sc, gc, highwayCost, 1.3f, 220000);

        // Full route: weave out of A, direct trunk, weave into B.
        std::vector<glm::ivec2> route = routeTownExit(tA, portA);
        for (const glm::ivec2& cc : trunk)
            route.push_back(glm::ivec2(cellWorld(cc.x), cellWorld(cc.y)));
        std::vector<glm::ivec2> exitB = routeTownExit(tB, portB);
        for (size_t ci = exitB.size(); ci-- > 0; )
            route.push_back(exitB[ci]);

        // A walled town opens a gate where this highway crosses its wall, so the
        // gateway lines up with the road that runs through it.
        if (tA.wallRadius > 0)
            plan.towns[e.first].gateAngles.push_back(
                crossingAngle(route, tA.center, (float)tA.wallRadius, false));
        if (tB.wallRadius > 0)
            plan.towns[e.second].gateAngles.push_back(
                crossingAngle(route, tB.center, (float)tB.wallRadius, true));

        emitHighwayRoute(plan, route);
    }
}

// Places street lights along the gravel paths between houses and along the
// first stretch of each highway leaving town. Stored as world-XZ positions;
// the prop streamer spawns a lantern-post Prop at each (prop_placement.cpp).
void placeStreetLamps(TownPlan& plan) {
    for (Town& t : plan.towns) {
        auto tooClose = [&](int wx, int wz) {
            for (const glm::ivec2& L : t.lampPosts)
                if (std::abs(L.x - wx) + std::abs(L.y - wz) < 11) return true;
            return false;
        };
        auto inBuilding = [&](int wx, int wz) {
            for (const TownBuilding& b : t.buildings)
                if (wx >= b.wx - 1 && wx < b.wx + b.dimX + 1 &&
                    wz >= b.wz - 1 && wz < b.wz + b.dimZ + 1) return true;
            return false;
        };
        int side = 0;
        // Walks a polyline placing a lamp every `spacing` blocks of *cumulative*
        // arc length (the polylines are made of short ~2-4 block segments).
        auto walkRoad = [&](const std::vector<glm::ivec2>& pts, int spacing,
                            float maxFromCentre) {
            float nextAt   = (float)spacing * 0.5f;
            float traveled = 0.0f;
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                glm::ivec2 a = pts[i], b = pts[i + 1];
                float dx = (float)(b.x - a.x), dz = (float)(b.y - a.y);
                float segLen = std::sqrt(dx * dx + dz * dz);
                if (segLen < 0.01f) continue;
                float perpX = -dz / segLen, perpZ = dx / segLen;
                while (nextAt <= traveled + segLen) {
                    float u  = (nextAt - traveled) / segLen;
                    int   px = a.x + (int)(dx * u), pz = a.y + (int)(dz * u);
                    nextAt  += (float)spacing;
                    if (maxFromCentre > 0.0f) {
                        float cdx = (float)(px - t.center.x);
                        float cdz = (float)(pz - t.center.y);
                        if (cdx * cdx + cdz * cdz > maxFromCentre * maxFromCentre)
                            continue;
                    }
                    int s  = (side++ & 1) ? 1 : -1;
                    // Offset clear of the widest a path ever varies to (see pathHalfWidth).
                    int lx = px + (int)(perpX * 5.2f * (float)s);
                    int lz = pz + (int)(perpZ * 5.2f * (float)s);
                    if (inBuilding(lx, lz) || tooClose(lx, lz)) continue;
                    t.lampPosts.push_back(glm::ivec2(lx, lz));
                }
                traveled += segLen;
            }
        };
        for (const TownRoad& p : t.paths)        walkRoad(p.pts, 15,   0.0f);
        for (const TownRoad& h : plan.highways)  walkRoad(h.pts, 20, 130.0f);

        // A ring of lamps around the central square.
        if (t.plazaR >= 12) {
            int n = std::max(4, t.plazaR / 6);
            for (int i = 0; i < n; i++) {
                float a  = (6.2831853f / (float)n) * (float)i + 0.39f;
                int   lx = t.center.x + (int)(cosf(a) * (float)(t.plazaR - 1));
                int   lz = t.center.y + (int)(sinf(a) * (float)(t.plazaR - 1));
                if (!inBuilding(lx, lz) && !tooClose(lx, lz))
                    t.lampPosts.push_back(glm::ivec2(lx, lz));
            }
        }
    }
}


}  // namespace townint
