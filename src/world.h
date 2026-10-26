#ifndef MC_WORLD_H
#define MC_WORLD_H
#include "reader.h"
#define MC_CACHE_SIDE 9
#define MC_CACHE_COUNT (MC_CACHE_SIDE*MC_CACHE_SIDE)
#define MC_MAX_HEIGHT 512
#define MC_MAX_DIMENSIONS 16
typedef struct { char name[128]; int min_y, height; } McDimension;
/* serial changes whenever a column's contents are replaced. */
typedef struct { int x,z; uint16_t *blocks; uint64_t serial; uint8_t *light; uint64_t light_known[2]; } McChunk;
typedef struct {
    McChunk chunks[MC_CACHE_COUNT];
    /* Per-slot bitmask of 16-block sections changed by block updates. Renderers clear it. */
    uint32_t dirty[MC_CACHE_COUNT];
    McDimension dimensions[MC_MAX_DIMENSIONS];
    int dimension_count, min_y, height, ready, loaded;
    uint64_t light_updates,decoded, updates, revision, center_updates, unloads, evictions;
    int center_x,center_z,view_distance,last_chunk_x,last_chunk_z;
    size_t bytes;
    double border_x,border_z,border_old,border_new;
    uint64_t border_started,border_duration;
    int border_warning_blocks,border_warning_time;
    char dimension[128], error[128];
} McWorld;
void mc_world_init(McWorld *w);
void mc_world_clear(McWorld *w);
void mc_world_destroy(McWorld *w);
/* Caller serializes mutation and rendering. Packets exclude the packet ID. */
int mc_world_packet(McWorld *w, uint32_t id, const uint8_t *p, size_t n);
const McChunk *mc_world_chunk(const McWorld *w,int chunk_x,int chunk_z);
uint16_t mc_world_get(const McWorld *w, int x,int y,int z);
/* Packed sky (low nibble) and block light (high); -1 until server light is known. */
int mc_world_light(const McWorld *w,int x,int y,int z);
int mc_world_set(McWorld *w, int x,int y,int z,uint16_t state);
double mc_world_border_size(const McWorld *w,uint64_t now);
int mc_palette(McReader *r, unsigned count, int biome, uint16_t *out);
#endif
