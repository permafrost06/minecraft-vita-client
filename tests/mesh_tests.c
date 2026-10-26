#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static McWorld world;
static McMesh mesh;
static const int16_t *no_heights[9];
static void put(int x, int y, int z, uint16_t s) { world.chunks[0].blocks[y*256+z*16+x] = s; }
static void clear(void) { memset(world.chunks[0].blocks, 0, 16*256*2); }
/* Each quad must be counter-clockwise when viewed from the side it faces. */
static int outward(const McVertex *v, float cx, float cy, float cz) {
    float ax = v[1].x-v[0].x, ay = v[1].y-v[0].y, az = v[1].z-v[0].z;
    float bx = v[2].x-v[0].x, by = v[2].y-v[0].y, bz = v[2].z-v[0].z;
    float nx = ay*bz-az*by, ny = az*bx-ax*bz, nz = ax*by-ay*bx;
    float mx = (v[0].x+v[2].x)/2.0f-cx, my = (v[0].y+v[2].y)/2.0f-cy, mz = (v[0].z+v[2].z)/2.0f-cz;
    return nx*mx+ny*my+nz*mz > 0;
}
int main(int argc, char **argv) {
    static uint32_t atlas[MC_ATLAS_SIZE*MC_ATLAS_SIZE];
    unsigned tiles = mc_textures_build(atlas);
    CHECK(tiles > 100 && tiles <= MC_ATLAS_TILES*MC_ATLAS_TILES);
    if (argc > 1) {
        FILE *f = fopen(argv[1], "wb"); CHECK(f != NULL);
        fprintf(f, "P6\n%d %d\n255\n", MC_ATLAS_SIZE, MC_ATLAS_SIZE);
        for (unsigned i = 0; i < MC_ATLAS_SIZE*MC_ATLAS_SIZE; i++) {
            uint32_t p = atlas[i], a = p >> 24; int checker = ((i % MC_ATLAS_SIZE)/4 + (i / MC_ATLAS_SIZE)/4) % 2 ? 90 : 60;
            uint8_t rgb[3];
            for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)((((p >> (c*8)) & 255)*a + checker*(255-a))/255);
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    mc_mesh_init(); CHECK(mc_mesh_alloc(&mesh) == 0);
    mc_world_init(&world); world.ready = 1; world.min_y = 0; world.height = 16;
    world.chunks[0] = (McChunk){0, 0, calloc(16*256, 2), 1}; world.loaded = 1;
    CHECK(mc_world_chunk(&world, 0, 0) != NULL);
    CHECK(mc_block_def(1)->model == MM_CUBE && (mc_block_def(1)->flags & MF_OPAQUE));
    CHECK(mc_block_def(0)->model == MM_NONE);
    /* One isolated stone block: six opaque faces, all wound outward. */
    put(5, 5, 5, 1); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.quads[MC_PASS_OPAQUE] == 6 && mesh.total == 6);
    for (unsigned q = 0; q < 6; q++) CHECK(outward(mesh.pass[0]+q*4, 5*16+8, 5*16+8, 5*16+8));
    /* A neighbor hides the shared faces. */
    put(6, 5, 5, 1); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.quads[MC_PASS_OPAQUE] == 10);
    /* Missing neighbor chunks count as solid: blocks on the edge omit that face. */
    clear(); put(0, 5, 5, 1); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.quads[MC_PASS_OPAQUE] == 5);
    /* Water: translucent surface lowered below a full block, sides shown to air. */
    clear(); put(5, 5, 5, 80); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.quads[MC_PASS_TRANSLUCENT] == 6 && mesh.quads[MC_PASS_OPAQUE] == 0);
    int top = 0; for (unsigned i = 0; i < 24; i++) if (mesh.pass[2][i].y > top) top = mesh.pass[2][i].y;
    CHECK(top == 5*16+14);
    /* Adjacent water hides the shared face. */
    put(5, 4, 5, 80); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.quads[MC_PASS_TRANSLUCENT] == 10);
    /* Ambient occlusion darkens a floor corner next to a wall. */
    clear(); for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) put(x, 2, z, 1);
    put(8, 3, 9, 1); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    int darker = 0;
    for (unsigned q = 0; q < mesh.quads[0]; q++) {
        McVertex *v = mesh.pass[0]+q*4;
        if (v[0].y == 3*16 && v[1].y == 3*16 && v[2].y == 3*16) for (int i = 0; i < 4; i++) if (v[i].r < 250) darker = 1;
    }
    CHECK(darker);
    /* Skylight: a face below an overhang is darker than an open one. */
    McHeightmap h; mc_mesh_heightmap(&world, &world.chunks[0], h);
    CHECK(h[5*16+5] == 3 && h[9*16+8] == 4);
    /* A grass plant produces two double-sided cutout quads. */
    clear(); put(3, 3, 3, 2001); mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mc_block_def(2001)->model == MM_CROSS && mesh.quads[MC_PASS_CUTOUT] == 2);
    /* Full sections stay within the 16-bit index limit. */
    for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) put(x, y, z, (x+y+z) % 2 ? 1 : 0);
    mc_mesh_section(&world, 0, 0, 0, no_heights, &mesh);
    CHECK(mesh.total <= MC_MESH_MAX_QUADS && mesh.total > 10000);
    /* Server light replaces the heightmap approximation, with block light in caves. */
    clear();put(5,5,5,1);McChunk *column=&world.chunks[0];
    column->light=calloc(48*256,1);CHECK(column->light);column->light_known[0]=column->light_known[1]=7;
    mc_mesh_section(&world,0,0,0,no_heights,&mesh);unsigned dark=mesh.pass[0][0].r;
    memset(column->light,0xf0,48*256);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mesh.pass[0][0].r>dark*5);
    memset(column->light,0x0f,48*256);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mesh.pass[0][0].r>dark*5);
    mc_world_destroy(&world); mc_mesh_free(&mesh);
    puts("mesh tests passed");
    return 0;
}
