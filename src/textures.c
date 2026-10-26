#include "blocks.h"
#include <math.h>
#include <string.h>
/* Original procedural 16x16 block textures. No Mojang assets are used. */
#define MC_BLOCKINFO_TEXTURES
#include "blockinfo.inc"
#undef MC_BLOCKINFO_TEXTURES

typedef struct { float r, g, b, a; } Px;
static uint32_t seed;
static float hash3(int x, int y, uint32_t s) {
    uint32_t h = (uint32_t)x*374761393u + (uint32_t)y*668265263u + s*2246822519u;
    h = (h ^ (h >> 13))*1274126177u; h ^= h >> 16;
    return (h & 0xffffff)/16777216.0f;
}
static float hash(int x, int y) { return hash3(x, y, seed); }
/* Tileable value noise with a lattice of `cell` pixels. */
static float vnoise(int x, int y, int cell, uint32_t salt) {
    int n = MC_TILE/cell, gx = x/cell, gy = y/cell;
    float fx = (x%cell + 0.5f)/cell, fy = (y%cell + 0.5f)/cell;
    fx = fx*fx*(3-2*fx); fy = fy*fy*(3-2*fy);
    float a = hash3(gx%n, gy%n, seed+salt), b = hash3((gx+1)%n, gy%n, seed+salt);
    float c = hash3(gx%n, (gy+1)%n, seed+salt), d = hash3((gx+1)%n, (gy+1)%n, seed+salt);
    return (a*(1-fx)+b*fx)*(1-fy) + (c*(1-fx)+d*fx)*fy;
}
static Px rgb(uint32_t c, float f) {
    return (Px){((c >> 16) & 255)*f, ((c >> 8) & 255)*f, (c & 255)*f, 255};
}
static Px mix(Px a, Px b, float t) { return (Px){a.r+(b.r-a.r)*t, a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t, a.a+(b.a-a.a)*t}; }
static const Px CLEAR = {0, 0, 0, 0};
static float stone_f(int x, int y) {
    return 0.8f + 0.22f*vnoise(x, y, 4, 1) + 0.12f*vnoise(x, y, 2, 2) + 0.1f*hash(x, y);
}
/* Distance to the nearest and second-nearest feature points on a wrapped 4x4 grid. */
static void cells(int x, int y, int n, float *d1, float *d2, int *id) {
    int cell = MC_TILE/n; *d1 = *d2 = 1e9f; *id = 0;
    for (int gy = -1; gy <= n; gy++) for (int gx = -1; gx <= n; gx++) {
        int wx = (gx+n)%n, wy = (gy+n)%n;
        float px = gx*cell + 1 + hash3(wx, wy, seed+7)*(cell-2), py = gy*cell + 1 + hash3(wx, wy, seed+8)*(cell-2);
        float dx = x+0.5f-px, dy = y+0.5f-py, d = sqrtf(dx*dx+dy*dy);
        if (d < *d1) { *d2 = *d1; *d1 = d; *id = wy*n+wx; } else if (d < *d2) *d2 = d;
    }
}
static Px pattern(const McTexDef *t, int x, int y) {
    uint32_t c1 = t->c1, c2 = t->c2; float h = hash(x, y);
    switch (t->pattern) {
    case TP_FLAT: return rgb(c1, 0.95f + 0.06f*h);
    case TP_STONE: return rgb(c1, stone_f(x, y) - (h < 0.05f ? 0.15f : 0));
    case TP_DIRT: {
        float f = 0.86f + 0.2f*vnoise(x, y, 4, 3) + 0.08f*h;
        if (h < 0.12f) f -= 0.18f; else if (h > 0.93f) f += 0.15f;
        return rgb(c1, f);
    }
    case TP_GRASS_TOP: return rgb(c1, 0.8f + 0.2f*vnoise(x, y, 4, 4) + 0.18f*h - (hash3(x, y, seed+5) < 0.1f ? 0.12f : 0));
    case TP_GRASS_SIDE: {
        int fringe = 3 + (hash3(x, 0, seed+9) < 0.55f) + (hash3(x, 1, seed+9) < 0.25f);
        if (y < fringe) return rgb(c2, 0.82f + 0.25f*h);
        float f = 0.86f + 0.2f*vnoise(x, y, 4, 3) + 0.08f*h;
        if (h < 0.12f) f -= 0.18f;
        return rgb(c1, y == fringe ? f*0.85f : f);
    }
    case TP_SAND: return rgb(c1, 0.92f + 0.1f*h - (hash3(x, y, seed+2) < 0.08f ? 0.1f : 0) + 0.05f*vnoise(x, y, 4, 2));
    case TP_GRAVEL: case TP_COBBLE: {
        float d1, d2; int id; cells(x, y, t->pattern == TP_GRAVEL ? 4 : 3, &d1, &d2, &id);
        float edge = d2-d1, tone = 0.75f + 0.35f*hash3(id, 0, seed+11);
        if (t->pattern == TP_COBBLE) {
            if (edge < 1.1f) return rgb(c1, 0.48f + 0.1f*h);
            return rgb(c1, tone + 0.1f*h - d1*0.035f + (edge < 2 ? -0.08f : 0.04f));
        }
        if (edge < 0.8f) return rgb(c1, 0.6f + 0.08f*h);
        Px p = rgb(c1, tone + 0.08f*h);
        if (hash3(id, 1, seed) < 0.25f) p = mix(p, rgb(0x9a7b62, tone), 0.5f);
        return p;
    }
    case TP_PLANKS: {
        int board = y/4, row = y%4, seam = (board*7 + 3) % 16;
        float grain = 0.9f + 0.1f*hash3(y, board, seed+12) + 0.05f*hash3(x/3, y, seed+13);
        if (row == 3) return rgb(c1, 0.62f);
        if (x == seam) return rgb(c1, 0.7f);
        if (row == 0) grain += 0.05f;
        return rgb(c1, grain - (h < 0.06f ? 0.15f : 0));
    }
    case TP_LOG_SIDE: {
        float f = 0.72f + 0.28f*hash3(x, 0, seed+14) + 0.12f*hash3(x, y/3, seed+15);
        if (hash3(x, y/2, seed+16) < 0.08f) f -= 0.25f;
        return rgb(c1, f);
    }
    case TP_LOG_TOP: {
        float dx = fabsf(x-7.5f), dy = fabsf(y-7.5f), d = dx > dy ? dx : dy;
        if (d >= 6.5f) return rgb(c2, 0.8f + 0.25f*h);
        return rgb(c1, ((int)d % 2 ? 0.86f : 1.0f) + 0.08f*h);
    }
    case TP_LEAVES: {
        if (h < 0.17f) return CLEAR;
        if (c2 && hash3(x, y, seed+17) > 0.86f) return rgb(c2, 0.9f + 0.2f*h);
        return rgb(c1, 0.62f + 0.42f*vnoise(x, y, 2, 6)*(0.7f + 0.3f*h) + 0.1f*h);
    }
    case TP_ORE: {
        Px stone = rgb(c1, stone_f(x, y));
        for (int i = 0; i < 4; i++) {
            float cx = 2 + hash3(i, 0, seed+18)*12, cy = 2 + hash3(i, 1, seed+18)*12;
            float dx = x+0.5f-cx, dy = y+0.5f-cy;
            if (dx*dx+dy*dy < 2.2f + hash3(x, y, seed+19)*1.6f) return rgb(c2, 0.82f + 0.3f*h - (dx+dy > 0.6f ? 0.15f : 0));
        }
        return stone;
    }
    case TP_BRICKS: {
        int row = y/4, offset = row % 2 ? 4 : 0;
        if (y % 4 == 3 || (x+offset) % 8 == 7) return rgb(c2, 0.92f + 0.1f*h);
        return rgb(c1, 0.86f + 0.12f*hash3((x+offset)/8, row, seed+20) + 0.1f*h - (y % 4 == 0 ? 0 : 0.03f));
    }
    case TP_STONE_BRICKS: {
        int row = y/8, offset = row % 2 ? 8 : 0, lx = (x+offset) % 16, ly = y % 8;
        if (ly == 7 || lx == 15) return rgb(c1, 0.58f + 0.06f*h);
        float f = stone_f(x, y)*0.95f;
        if (ly == 0 || lx == 0) f += 0.1f; else if (ly == 6 || lx == 14) f -= 0.08f;
        return rgb(c1, f);
    }
    case TP_TILES: case TP_METAL: case TP_GLOW: {
        float f = t->pattern == TP_GLOW ? 0.82f + 0.28f*vnoise(x, y, 4, 21) + 0.1f*h : 0.96f + 0.05f*h;
        if (x == 0 || y == 0) f += 0.1f; else if (x == 15 || y == 15) f -= 0.14f;
        if (t->pattern == TP_METAL && (x+y*2) % 11 == 0) f += 0.07f;
        if (t->pattern == TP_METAL && (x == 1 || y == 1)) f += 0.05f;
        return rgb(c1, f);
    }
    case TP_WOOL: return rgb(c1, 0.9f + 0.08f*((x+y) % 2) + 0.06f*vnoise(x, y, 4, 22) + 0.04f*h);
    case TP_GLASS: {
        if (x == 0 || y == 0 || x == 15 || y == 15) return rgb(c1, 0.85f + 0.1f*h);
        if ((x == y+3 && x > 4 && x < 9) || (x == y+4 && x > 5 && x < 11) || (x+1 == y-7 && x > 3 && x < 7))
            return (Px){255, 255, 255, 140};
        return CLEAR;
    }
    case TP_STAINED_GLASS: {
        Px p = rgb(c1, 0.95f + 0.05f*h);
        p.a = (x == 0 || y == 0 || x == 15 || y == 15) ? 230 : 120;
        if (x == y+3 && x > 4 && x < 10) p = mix(p, (Px){255, 255, 255, 170}, 0.5f);
        return p;
    }
    case TP_WATER: {
        Px p = rgb(c1, 0.85f + 0.22f*vnoise(x, y, 4, 23) + 0.06f*h);
        p.a = 165; return p;
    }
    case TP_LAVA: {
        float n = vnoise(x, y, 4, 24)*0.7f + vnoise(x, y, 2, 25)*0.3f;
        return n > 0.62f ? mix(rgb(c1, 1.1f), rgb(0xffd23a, 1), (n-0.62f)*2.6f) : rgb(c1, 0.8f + 0.3f*n);
    }
    case TP_PLANT: case TP_CROP: {
        int blades = t->pattern == TP_PLANT ? 7 : 5;
        for (int i = 0; i < blades; i++) {
            int bx = (int)(1 + hash3(i, 0, seed+26)*14), top = (int)(2 + hash3(i, 1, seed+26)*9);
            if (t->pattern == TP_CROP) { bx = 1 + i*3; top = 1 + (int)(hash3(i, 1, seed+26)*5); }
            int lean = (int)((15-y)*(hash3(i, 2, seed+26)-0.5f)*0.5f);
            if (y >= top && x == bx+lean) return rgb(c1, 0.75f + 0.35f*(15-y)/15.0f + 0.1f*h);
            if (t->pattern == TP_CROP && y >= top && y < top+3 && (x == bx+lean+1)) return rgb(c1, 0.95f);
        }
        return CLEAR;
    }
    case TP_FLOWER: {
        float dx = x-7.5f, dy = y-5.0f, d = dx*dx+dy*dy;
        if (d < 1.3f) return rgb(0xf0d040, 0.9f + 0.1f*h);
        if (d < 8.5f && h > 0.08f) return rgb(c2, 0.85f + 0.25f*h - d*0.02f);
        if ((x == 7 || x == 8) && y >= 7) return rgb(c1, 0.85f + 0.15f*h);
        if (y == 11 && x >= 4 && x <= 6) return rgb(c1, 0.9f);
        if (y == 12 && x >= 9 && x <= 11) return rgb(c1, 0.9f);
        return CLEAR;
    }
    case TP_SNOW: return rgb(c1, 0.95f + 0.05f*h - 0.04f*vnoise(x, y, 4, 27));
    case TP_ICE: { Px p = rgb(c1, 0.92f + 0.12f*vnoise(x, y, 4, 28) + (x == y || x == y+5 ? 0.12f : 0)); p.a = 190; return p; }
    case TP_ICE_SOLID: return rgb(c1, 0.92f + 0.08f*h + ((x*3+y) % 13 == 0 ? 0.12f : 0) - 0.08f*vnoise(x, y, 4, 29));
    case TP_BEDROCK: return rgb(c1, 0.45f + 0.75f*(0.6f*vnoise(x, y, 2, 30) + 0.4f*h));
    case TP_OBSIDIAN: return rgb(c1, 0.85f + 0.3f*vnoise(x, y, 4, 31) + (h > 0.92f ? 0.9f : 0));
    case TP_SANDSTONE: {
        float f = 0.93f + 0.06f*h;
        if (y < 3) f += 0.06f; else if (y == 3 || y == 12) f -= 0.12f; else if (y > 12) f -= 0.04f;
        return rgb(c1, f);
    }
    case TP_GLAZED: {
        int a = (x/4 + y/4) % 2, edge = (x % 8 == 0) || (y % 8 == 0);
        return rgb(edge ? c2 : a ? c1 : c2, edge ? 0.8f : 0.95f + 0.08f*h);
    }
    case TP_MACHINE: {
        float f = 0.92f + 0.08f*h;
        if (x == 0 || y == 0 || x == 15 || y == 15) return rgb(c1, 0.7f);
        if (x >= 4 && x <= 11 && y >= 5 && y <= 11) return rgb(c2, (x == 4 || y == 5) ? 0.7f : 0.95f + 0.1f*h);
        return rgb(c1, f);
    }
    case TP_BOOKS: {
        if (y == 0 || y == 15 || y == 7 || y == 8) return rgb(0xa2834f, 0.85f + 0.1f*h);
        if (x == 0 || x == 15) return rgb(0xa2834f, 0.7f);
        static const uint32_t spines[] = {0x8a2a22, 0x2a4a8a, 0x2a7a3a, 0x8a6a2a, 0x5a2a6a, 0x7a3a2a};
        int book = (x + (y > 7 ? 2 : 0))/2;
        return rgb(spines[(book + (y > 7)) % 6], (x % 2 ? 0.8f : 1.0f) - (y == 1 || y == 9 ? 0.15f : 0));
    }
    case TP_CACTUS: {
        float f = 0.85f + 0.1f*h + (x % 4 == 1 ? -0.15f : 0);
        if (x % 4 == 2 && y % 5 == 1) return rgb(0xe8e0b0, 1);
        return rgb(c1, f);
    }
    case TP_TORCH:
        if (x < 7 || x > 8) return CLEAR;
        if (y >= 6 && y <= 7) return rgb(c2, 1.0f + 0.1f*h);
        if (y >= 8) return rgb(c1, 0.85f + 0.15f*h);
        return CLEAR;
    case TP_RAIL:
        if (x == 2 || x == 3 || x == 12 || x == 13) return rgb(c1, x == 3 || x == 13 ? 0.75f : 1.0f);
        if (y % 4 == 1 || y % 4 == 2) return rgb(c2, 0.85f + 0.1f*h);
        return CLEAR;
    case TP_WIRE:
        if ((x == 7 || x == 8) || (y == 7 || y == 8 ? h < 0.3f : 0)) return rgb(c1, 0.8f + 0.3f*h);
        return CLEAR;
    }
    return rgb(0xff00ff, 1);
}
static uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
unsigned mc_textures_build(uint32_t atlas[MC_ATLAS_SIZE*MC_ATLAS_SIZE]) {
    unsigned count = sizeof(tex_defs)/sizeof(tex_defs[0]);
    if (count > MC_ATLAS_TILES*MC_ATLAS_TILES) count = MC_ATLAS_TILES*MC_ATLAS_TILES;
    memset(atlas, 0, sizeof(uint32_t)*MC_ATLAS_SIZE*MC_ATLAS_SIZE);
    for (unsigned i = 0; i < count; i++) {
        const McTexDef *t = &tex_defs[i]; seed = t->c1*31u + t->c2*17u + t->pattern*1013u;
        unsigned ox = (i % MC_ATLAS_TILES)*MC_TILE, oy = (i / MC_ATLAS_TILES)*MC_TILE;
        for (int y = 0; y < MC_TILE; y++) for (int x = 0; x < MC_TILE; x++) {
            Px p = pattern(t, x, y);
            atlas[(oy+y)*MC_ATLAS_SIZE + ox+x] = clamp8(p.r) | clamp8(p.g) << 8 | clamp8(p.b) << 16 | (uint32_t)clamp8(p.a) << 24;
        }
    }
    return count;
}
uint32_t mc_block_color(uint16_t state) {
    const McBlockDef *def = mc_block_def(state);
    if (def->model == MM_NONE || def->side >= sizeof(tex_defs)/sizeof(tex_defs[0])) return 0;
    return tex_defs[def->side].c1 & 0xffffff;
}
