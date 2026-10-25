#include "world.h"
#include "nbt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int bad(McWorld *w, const char *message) { snprintf(w->error,sizeof(w->error),"%s",message); return -1; }
void mc_world_init(McWorld *w) { memset(w,0,sizeof(*w)); }
void mc_world_clear(McWorld *w) {
    for (int i = 0; i < MC_CACHE_COUNT; i++) { free(w->chunks[i].blocks); w->chunks[i].blocks = NULL; }
    w->loaded = 0; w->bytes = 0; w->revision++;
}
void mc_world_destroy(McWorld *w) { mc_world_clear(w); }
static unsigned slot(int x,int z) {
    int a = x % MC_CACHE_SIDE, b = z % MC_CACHE_SIDE;
    if (a < 0) a += MC_CACHE_SIDE;
    if (b < 0) b += MC_CACHE_SIDE;
    return b*MC_CACHE_SIDE+a;
}
static int floor16(int n) { int q = n/16; return q-(n%16 < 0); }
uint16_t mc_world_get(const McWorld *w,int x,int y,int z) {
    if (!w->ready || y < w->min_y || y >= w->min_y+w->height) return 0;
    int cx = floor16(x), cz = floor16(z);
    const McChunk *c = &w->chunks[slot(cx,cz)];
    if (!c->blocks || c->x != cx || c->z != cz) return 0;
    unsigned lx = (unsigned)x & 15, lz = (unsigned)z & 15;
    return c->blocks[(y-w->min_y)*256+lz*16+lx];
}
int mc_world_set(McWorld *w,int x,int y,int z,uint16_t state) {
    if (!w->ready || y < w->min_y || y >= w->min_y+w->height) return 0;
    int cx = floor16(x), cz = floor16(z); McChunk *c = &w->chunks[slot(cx,cz)];
    if (!c->blocks || c->x != cx || c->z != cz) return 0;
    c->blocks[(y-w->min_y)*256+((unsigned)z & 15)*16+((unsigned)x & 15)] = state;
    w->updates++; w->revision++; return 0;
}
int mc_palette(McReader *r,unsigned count,int biome,uint16_t *out) {
    unsigned bits = mc_u8(r); uint32_t palette[256]; unsigned entries = 0;
    unsigned limit = biome ? 3 : 8;
    if (r->error || bits > (biome ? 6u : 15u)) return -1;
    if (bits == 0) {
        uint32_t state = mc_varint_read(r), words = mc_varint_read(r);
        if (r->error || words || (!biome && state > 65535)) return -1;
        if (out) for (unsigned i = 0; i < count; i++) out[i] = state;
        return 0;
    }
    if (!biome && bits < 4) return -1;
    if (bits <= limit) {
        entries = mc_varint_read(r);
        if (r->error || !entries || entries > (1u << bits)) return -1;
        for (unsigned i = 0; i < entries; i++) {
            palette[i] = mc_varint_read(r);
            if (r->error || (!biome && palette[i] > 65535)) return -1;
        }
    }
    unsigned per_long = 64/bits, expected = (count+per_long-1)/per_long;
    unsigned words = mc_varint_read(r);
    if (r->error || words != expected || words > (r->n-r->pos)/8) return -1;
    uint64_t mask = (1ull << bits)-1;
    unsigned index = 0;
    for (unsigned i = 0; i < words; i++) {
        uint64_t data = mc_u64(r);
        for (unsigned j = 0; j < per_long && index < count; j++,index++) {
            uint32_t v = (data >> (j*bits)) & mask;
            if (entries) { if (v >= entries) return -1; v = palette[v]; }
            if (out) out[index] = v;
        }
    }
    return r->error ? -1 : 0;
}
static int select_dimension(McWorld *w,const char *name) {
    for (int i = 0; i < w->dimension_count; i++) {
        McDimension *d = &w->dimensions[i];
        if (!strcmp(d->name,name)) {
            if (d->height < 16 || d->height > MC_MAX_HEIGHT || d->height%16 || d->min_y%16 ||
                d->min_y < -2048 || d->min_y > 2048) return bad(w,"Unsupported dimension height (maximum 512 blocks)");
            mc_world_clear(w); w->min_y = d->min_y; w->height = d->height; w->ready = 1;
            snprintf(w->dimension,sizeof(w->dimension),"%s",name); return 0;
        }
    }
    return bad(w,"Dimension is missing from the server registry");
}
static int join(McWorld *w,McReader *r) {
    mc_skip(r,7); unsigned worlds = mc_varint_read(r);
    if (r->error || worlds > 1024) return bad(w,"Invalid world-name list");
    for (unsigned i = 0; i < worlds; i++) { unsigned n = mc_varint_read(r); mc_skip(r,n); }
    McNbt codec,registry,list;
    if (mc_nbt_root(r,&codec) < 0 || mc_nbt_find(codec,"minecraft:dimension_type",&registry) < 0 ||
        mc_nbt_find(registry,"value",&list) < 0 || list.type != 9) return bad(w,"Invalid dimension registry NBT");
    McReader a = {list.data,list.size,0,0}; unsigned type = mc_u8(&a), count = mc_u32(&a);
    if (type != 10 || !count || count > MC_MAX_DIMENSIONS) return bad(w,"Too many server dimensions");
    McDimension dimensions[MC_MAX_DIMENSIONS];
    for (unsigned i = 0; i < count; i++) {
        McNbt entry,name,element,min_y,height;
        if (mc_nbt_payload(&a,type,0,&entry) < 0 || mc_nbt_find(entry,"name",&name) < 0 ||
            mc_nbt_string(name,dimensions[i].name,sizeof(dimensions[i].name)) < 0 ||
            mc_nbt_find(entry,"element",&element) < 0 || mc_nbt_find(element,"min_y",&min_y) < 0 ||
            mc_nbt_find(element,"height",&height) < 0 || mc_nbt_int(min_y,&dimensions[i].min_y) < 0 ||
            mc_nbt_int(height,&dimensions[i].height) < 0) return bad(w,"Invalid dimension bounds");
    }
    char dimension[128];
    if (mc_string(r,dimension,sizeof(dimension)) < 0) return bad(w,"Invalid active dimension");
    memcpy(w->dimensions,dimensions,count*sizeof(*dimensions)); w->dimension_count = count;
    return select_dimension(w,dimension);
}
static int chunk(McWorld *w,McReader *r) {
    if (!w->ready) return bad(w,"Chunk received before dimension registry");
    int x = (int32_t)mc_u32(r), z = (int32_t)mc_u32(r); McNbt heightmaps;
    if (mc_nbt_root(r,&heightmaps) < 0) return bad(w,"Invalid chunk heightmap NBT");
    unsigned length = mc_varint_read(r);
    if (r->error || length > r->n-r->pos) return bad(w,"Truncated chunk sections");
    McReader sections = {r->p+r->pos,length,0,0};
    uint16_t *blocks = malloc((size_t)w->height*256*sizeof(uint16_t));
    if (!blocks) return bad(w,"Chunk cache allocation failed");
    for (int y = 0; y < w->height/16; y++) {
        unsigned non_air = mc_u16(&sections);
        if (non_air > 4096 || mc_palette(&sections,4096,0,blocks+y*4096) < 0 ||
            mc_palette(&sections,64,1,NULL) < 0) { free(blocks); return bad(w,"Invalid chunk palette or packed data"); }
    }
    /* Vanilla can over-allocate its section byte array and leave zero padding.
       Read exactly the dimension's sections; permit only bounded zero padding. */
    if (sections.n-sections.pos > (size_t)(w->height/16)*8) sections.error = 1;
    while (sections.pos < sections.n) if (mc_u8(&sections) != 0) sections.error = 1;
    if (sections.error) { free(blocks); return bad(w,"Invalid trailing chunk section bytes"); }
    McChunk *c = &w->chunks[slot(x,z)]; size_t bytes = (size_t)w->height*256*sizeof(uint16_t);
    if (c->blocks) free(c->blocks); else { w->loaded++; w->bytes += bytes; }
    *c = (McChunk){x,z,blocks}; w->decoded++; w->revision++; return 0;
}
static int sign_bits(uint64_t n,unsigned bits) { uint64_t mask = 1ull << (bits-1); return (int)((int64_t)(n ^ mask)-(int64_t)mask); }
int mc_world_packet(McWorld *w,uint32_t id,const uint8_t *p,size_t n) {
    McReader r = {p,n,0,0};
    if (id == 0x28) return join(w,&r);
    if (id == 0x41) {
        char name[128]; if (mc_string(&r,name,sizeof(name)) < 0) return bad(w,"Invalid respawn dimension");
        return select_dimension(w,name);
    }
    if (id == 0x24) return chunk(w,&r);
    if (id == 0x1e) {
        int x = (int32_t)mc_u32(&r), z = (int32_t)mc_u32(&r);
        if (r.error || r.pos != n) return bad(w,"Invalid chunk unload");
        McChunk *c = &w->chunks[slot(x,z)];
        if (c->blocks && c->x == x && c->z == z) {
            free(c->blocks); c->blocks = NULL; w->loaded--; w->bytes -= (size_t)w->height*256*2; w->revision++;
        }
    } else if (id == 0x0a) {
        uint64_t pos = mc_u64(&r); uint32_t state = mc_varint_read(&r);
        if (r.error || r.pos != n || state > 65535) return bad(w,"Invalid block update");
        mc_world_set(w,sign_bits(pos >> 38,26),sign_bits(pos & 4095,12),sign_bits((pos >> 12) & 0x3ffffff,26),state);
    } else if (id == 0x43) {
        uint64_t pos = mc_u64(&r); mc_u8(&r); unsigned count = mc_varint_read(&r);
        if (r.error || count > 4096) return bad(w,"Invalid section update count");
        int cx = sign_bits(pos >> 42,22), cz = sign_bits((pos >> 20) & 0x3fffff,22), cy = sign_bits(pos & 0xfffff,20);
        uint64_t records[4096];
        for (unsigned i = 0; i < count; i++) {
            records[i] = mc_varlong(&r);
            if (r.error || (records[i] >> 12) > 65535) return bad(w,"Invalid section update record");
        }
        if (r.pos != n) return bad(w,"Section update length mismatch");
        for (unsigned i = 0; i < count; i++) {
            uint64_t v = records[i]; mc_world_set(w,cx*16+(int)((v >> 8) & 15),cy*16+(int)(v & 15),cz*16+(int)((v >> 4) & 15),v >> 12);
        }
    }
    return 0;
}
