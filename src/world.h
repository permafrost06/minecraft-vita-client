#ifndef MC_WORLD_H
#define MC_WORLD_H
#include "reader.h"
#define MC_CACHE_SIDE 9
#define MC_CACHE_COUNT (MC_CACHE_SIDE*MC_CACHE_SIDE)
#define MC_MAX_HEIGHT 512
#define MC_MAX_DIMENSIONS 16
typedef struct { char name[128]; int min_y, height; } McDimension;
typedef struct { int x,z; uint16_t *blocks; } McChunk;
typedef struct {
    McChunk chunks[MC_CACHE_COUNT];
    McDimension dimensions[MC_MAX_DIMENSIONS];
    int dimension_count, min_y, height, ready, loaded;
    uint64_t decoded, updates, revision;
    size_t bytes;
    char dimension[128], error[128];
} McWorld;
void mc_world_init(McWorld *w);
void mc_world_clear(McWorld *w);
void mc_world_destroy(McWorld *w);
/* Caller serializes mutation and rendering. Packets exclude the packet ID. */
int mc_world_packet(McWorld *w, uint32_t id, const uint8_t *p, size_t n);
const McChunk *mc_world_chunk(const McWorld *w,int chunk_x,int chunk_z);
uint16_t mc_world_get(const McWorld *w, int x,int y,int z);
int mc_world_set(McWorld *w, int x,int y,int z,uint16_t state);
int mc_palette(McReader *r, unsigned count, int biome, uint16_t *out);
#endif
