#ifndef MC_WORLD_H
#define MC_WORLD_H
#include "reader.h"
#include "environment.h"
#include "nbt.h"
#define MC_BLOCK_ENTITIES 1024
#define MC_BLOCK_ENTITY_NBT 16384
#define MC_BLOCK_ENTITY_BYTES 262144
typedef struct { uint32_t pos; uint32_t type; uint8_t *nbt; size_t size; uint8_t action, parameter; } McBlockEntity;
#define MC_CACHE_SIDE 9
#define MC_CACHE_COUNT (MC_CACHE_SIDE*MC_CACHE_SIDE)
#define MC_MAX_HEIGHT 512
#define MC_MAX_DIMENSIONS 16
typedef struct { char name[128]; int min_y, height; int has_sky, fixed; float ambient; uint64_t fixed_time; } McDimension;
/* serial changes whenever a column's contents are replaced. */
typedef struct { int x,z; uint16_t *blocks; uint64_t serial; uint8_t *light; uint64_t light_known[2]; McBlockEntity *entities; unsigned entity_count; size_t entity_bytes; } McChunk;
typedef struct {
    McChunk chunks[MC_CACHE_COUNT];
    McEnvironment environment;
    uint64_t block_entity_updates;
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
    /* Biome colours (0xRRGGBB) by registry id from Join Game; biome_count 0 when unknown. */
#define MC_MAX_BIOMES 256
    uint32_t biome_grass[MC_MAX_BIOMES],biome_foliage[MC_MAX_BIOMES],biome_water[MC_MAX_BIOMES];
    int biome_count,plains_biome;uint64_t biome_updates;
    char dimension[128], error[128];
} McWorld;
const McBlockEntity *mc_world_block_entity(const McWorld *w,int x,int y,int z);
int mc_block_entity_sign_line(const McBlockEntity *e,unsigned line,char *out,size_t capacity);
McNbt mc_block_entity_nbt(const McBlockEntity *e);
void mc_world_init(McWorld *w);
void mc_world_clear(McWorld *w);
void mc_world_destroy(McWorld *w);
/* Caller serializes mutation and rendering. Packets exclude the packet ID. */
int mc_world_packet(McWorld *w, uint32_t id, const uint8_t *p, size_t n);
/* Biome registry id at a block (4x4x4 cells), or -1 when unloaded. */
int mc_world_biome(const McWorld *w,int x,int y,int z);
/* Original approximation of vanilla's temperature/downfall colour maps. */
uint32_t mc_biome_grass_color(float temperature,float downfall);
uint32_t mc_biome_foliage_color(float temperature,float downfall);
const McChunk *mc_world_chunk(const McWorld *w,int chunk_x,int chunk_z);
uint16_t mc_world_get(const McWorld *w, int x,int y,int z);
/* Packed sky (low nibble) and block light (high); -1 until server light is known. */
int mc_world_light(const McWorld *w,int x,int y,int z);
int mc_world_set(McWorld *w, int x,int y,int z,uint16_t state);
double mc_world_border_size(const McWorld *w,uint64_t now);
int mc_palette(McReader *r, unsigned count, int biome, uint16_t *out);
#endif
