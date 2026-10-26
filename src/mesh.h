#ifndef MC_MESH_H
#define MC_MESH_H
#include "world.h"
#include "blocks.h"
/* Positions are in 1/16 block units relative to the section origin. UVs are in
   1/16 atlas-texel units. 16-bit indices limit one section to 16384 quads. */
#define MC_MESH_MAX_QUADS 16384
/* RGB stores daylight brightness, sky level and block level for dynamic lighting. */
typedef struct { int16_t x, y, z, pad; int16_t u, v; uint8_t r, g, b, a; } McVertex;
typedef struct {
    McVertex *pass[MC_PASSES];
    unsigned quads[MC_PASSES], total, dropped;
} McMesh;
/* Lowest Y that has open sky above it for each column; used for cheap skylight. */
typedef int16_t McHeightmap[256];
void mc_mesh_init(void);
int mc_mesh_alloc(McMesh *m);
void mc_mesh_free(McMesh *m);
void mc_mesh_heightmap(const McWorld *w, const McChunk *c, McHeightmap out);
/* heights is the 3x3 neighborhood indexed (dz+1)*3+(dx+1); entries may be NULL.
   The caller serializes access to the world. */
void mc_mesh_section(const McWorld *w, int cx, int sy, int cz, const int16_t *const heights[9], McMesh *out);
/* Split form for threaded meshing: gather copies everything needed from the
   world (caller holds the world lock); build needs no lock. build uses static
   scratch, so only one thread may call it (or mc_mesh_section) at a time. */
#define MC_MESH_PAD 18
typedef struct {
    uint16_t grid[MC_MESH_PAD*MC_MESH_PAD*MC_MESH_PAD];
    int16_t sky[MC_MESH_PAD*MC_MESH_PAD];
    uint16_t light[MC_MESH_PAD*MC_MESH_PAD*MC_MESH_PAD];
    uint8_t open[4096];
    int base_y, empty;
} McMeshInput;
void mc_mesh_gather(const McWorld *w, int cx, int sy, int cz, const int16_t *const heights[9], McMeshInput *in);
void mc_mesh_build(const McMeshInput *in, McMesh *out);
#endif
