#include "mesh.h"
#include "player.h"
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
    world.chunks[0] = (McChunk){0, 0, calloc(16*256*2+64, 1), 1}; world.loaded = 1;
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
    mc_mesh_section(&world,0,0,0,no_heights,&mesh);unsigned dark=mesh.pass[0][0].r;CHECK(dark>0);
    CHECK(mesh.pass[0][0].g==0 && mesh.pass[0][0].b==0);
    memset(column->light,0xf0,48*256);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mesh.pass[0][0].r>dark*5);CHECK(mesh.pass[0][0].b==255 && mesh.pass[0][0].g==0);
    memset(column->light,0x0f,48*256);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mesh.pass[0][0].r>dark*5);CHECK(mesh.pass[0][0].g==255 && mesh.pass[0][0].b==0);
    memset(column->light,0xff,48*256);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mesh.pass[0][0].g==255 && mesh.pass[0][0].b==255);
    /* Dedicated signs have geometry despite empty collision shapes. Rotation
       changes the board plane, and chest actions are copied into worker input. */
    clear();put(5,5,5,4298);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mc_block_def(4298)->model==MM_SIGN && mesh.quads[1]==12);
    int z0=mesh.pass[1][24].z;
    put(5,5,5,4306);mc_mesh_section(&world,0,0,0,no_heights,&mesh);CHECK(mesh.pass[1][24].z!=z0);
    /* Biome tint: grass block tops and plants take the biome/plains colour ratio; sides do not. */
    world.biome_count=2;world.plains_biome=0;
    world.biome_grass[0]=mc_biome_grass_color(.8f,.4f);world.biome_foliage[0]=mc_biome_foliage_color(.8f,.4f);world.biome_water[0]=0x3f76e4;
    world.biome_grass[1]=mc_biome_grass_color(2,0);world.biome_foliage[1]=mc_biome_foliage_color(2,0);world.biome_water[1]=0x3f76e4;
    clear();put(5,5,5,9);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    for(unsigned q=0;q<mesh.quads[0];q++)CHECK(mesh.pass[0][q*4].pad==0);
    uint8_t *biomes=(uint8_t *)(world.chunks[0].blocks+16*256);memset(biomes,1,64);
    mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    int tinted=0,plain=0;for(unsigned q=0;q<mesh.quads[0];q++){if(mesh.pass[0][q*4].pad<0)tinted++;else plain++;}
    CHECK(tinted==1 && plain==5);
    CHECK(mc_world_biome(&world,5,5,5)==1 && mc_world_biome(&world,5,16,5)==-1);
    clear();put(5,5,5,2352);mc_mesh_section(&world,0,0,0,no_heights,&mesh);CHECK(mesh.pass[1][0].pad==0);
    memset(biomes,0,64);world.biome_count=0;
    /* Blocks without collision use generated outline boxes; waterlogged states add water. */
    clear();put(5,5,5,2352);mc_mesh_section(&world,0,0,0,no_heights,&mesh);CHECK(mesh.quads[1]==6 && !mesh.quads[2]);
    unsigned count;const McBlockBox *button=mc_block_render_boxes(8607,&count);CHECK(count==1 && button->bounds[1]==0 && button->bounds[4]==2);
    CHECK(mc_block_render_boxes(8608,&count) && count==1);
    button=mc_block_render_boxes(1,&count);CHECK(count==1 && button->bounds[3]==32);
    clear();put(5,5,5,2870);mc_mesh_section(&world,0,0,0,no_heights,&mesh);CHECK(mesh.quads[2]>0);
    clear();put(5,5,5,2871);mc_mesh_section(&world,0,0,0,no_heights,&mesh);CHECK(!mesh.quads[2]);
    clear();put(5,5,5,2951);mc_mesh_section(&world,0,0,0,no_heights,&mesh);
    CHECK(mc_block_def(2951)->model==MM_CHEST && mesh.total==18);
    column->entities=calloc(1,sizeof(McBlockEntity));column->entity_count=1;
    column->entities[0]=(McBlockEntity){.pos=5*256+5*16+5,.action=1,.parameter=1};
    mc_mesh_section(&world,0,0,0,no_heights,&mesh);int high=0;
    for(unsigned i=0;i<mesh.quads[0]*4;i++)if(mesh.pass[0][i].y>6*16)high=1;CHECK(high);
    mc_world_destroy(&world); mc_mesh_free(&mesh);
    puts("mesh tests passed");
    return 0;
}
