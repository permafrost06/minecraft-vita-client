#include "world.h"
#include "nbt.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failed: %s at %d\n",#x,__LINE__); exit(1); } } while (0)
static uint8_t data[131072]; static size_t length;
static void u8(unsigned v) { data[length++] = v; }
static void u16(unsigned v) { u8(v >> 8); u8(v); }
static void u32(uint32_t v) { u16(v >> 16); u16(v); }
static void u64(uint64_t v) { u32(v >> 32); u32(v); }
static void var(unsigned v) { length += mc_varint_write(data+length,v); }
static void str(const char *s) { var(strlen(s)); memcpy(data+length,s,strlen(s)); length += strlen(s); }
static void name(unsigned type,const char *s) { u8(type); u16(strlen(s)); memcpy(data+length,s,strlen(s)); length += strlen(s); }
static void nstr(const char *key,const char *v) { name(8,key); u16(strlen(v)); memcpy(data+length,v,strlen(v)); length += strlen(v); }
static void registry(void) {
    length = 0; u32(1); u8(0); u8(1); u8(255); var(1); str("minecraft:overworld");
    name(10,""); name(10,"minecraft:dimension_type"); name(9,"value"); u8(10); u32(1);
    nstr("name","minecraft:overworld"); name(10,"element");
    name(3,"min_y"); u32((uint32_t)-64); name(3,"height"); u32(384);
    u8(0); u8(0); u8(0); u8(0); str("minecraft:overworld");
}
static void chunk(int x,int z,unsigned padding) {
    length = 0; u32(x); u32(z); name(10,""); u8(0); var(24*8+padding);
    for (int i = 0; i < 24; i++) {
        u16(i == 0 ? 4096 : 0); u8(0); var(i == 0 ? 1 : 0); var(0); u8(0); var(0); var(0);
    }
    for (unsigned i = 0; i < padding; i++) u8(0);
}
static void palettes(void) {
    uint16_t decoded[4096];
    for (unsigned bits = 4; bits <= 15; bits++) {
        if (bits > 8 && bits != 15) continue;
        length = 0; u8(bits); unsigned entries = bits <= 8 ? (1u << bits) : 0;
        if (entries) { var(entries); for (unsigned i = 0; i < entries; i++) var(100+i); }
        unsigned per = 64/bits, words = (4096+per-1)/per;
        var(words);
        for (unsigned i = 0; i < words; i++) {
            uint64_t v = 0;
            for (unsigned j = 0; j < per && i*per+j < 4096; j++) {
                unsigned index = i*per+j, value = entries ? index%entries : index;
                v |= (uint64_t)value << (bits*j);
            }
            u64(v);
        }
        McReader r = {data,length,0,0}; CHECK(mc_palette(&r,4096,0,decoded) == 0); CHECK(r.pos == length);
        for (unsigned i = 0; i < 4096; i++) CHECK(decoded[i] == (entries ? 100+i%entries : i));
        r = (McReader){data,length-1,0,0}; CHECK(mc_palette(&r,4096,0,decoded) < 0);
    }
    length = 0; u8(0); var(512); var(0); McReader r = {data,length,0,0}; CHECK(mc_palette(&r,4096,0,decoded) == 0); CHECK(decoded[4095] == 512);
    data[length-1] = 1; r = (McReader){data,length,0,0}; CHECK(mc_palette(&r,4096,0,decoded) < 0);
    length = 0; u8(4); var(1); var(1); var(256); for (int i = 0; i < 256; i++) u64(0xffffffffffffffffull);
    r = (McReader){data,length,0,0}; CHECK(mc_palette(&r,4096,0,decoded) < 0);
}
int main(void) {
    palettes(); McWorld world; mc_world_init(&world);
    registry(); CHECK(mc_world_packet(&world,0x28,data,length) == 0);
    CHECK(world.ready && world.height == 384 && world.min_y == -64);
    for (size_t i = 0; i < length; i++) {
        McWorld invalid; mc_world_init(&invalid); CHECK(mc_world_packet(&invalid,0x28,data,i) < 0); mc_world_destroy(&invalid);
    }
    chunk(-1,-1,16); CHECK(mc_world_packet(&world,0x24,data,length) == 0);
    CHECK(world.loaded == 1 && world.bytes == 384*256*2);
    CHECK(mc_world_get(&world,-16,-64,-16) == 1); CHECK(mc_world_get(&world,-1,-49,-1) == 1);
    CHECK(mc_world_get(&world,-1,-48,-1) == 0); CHECK(mc_world_get(&world,-1,-65,-1) == 0);
    CHECK(mc_world_get(&world,0,-64,0) == 0);
    for (size_t i = 0; i < length-16; i++) CHECK(mc_world_packet(&world,0x24,data,i) < 0);
    CHECK(world.loaded == 1 && mc_world_get(&world,-1,-64,-1) == 1);
    data[length-1] = 1; CHECK(mc_world_packet(&world,0x24,data,length) < 0);
    CHECK(mc_world_get(&world,-1,-64,-1) == 1);
    length = 0;
    uint64_t pos = ((uint64_t)((uint32_t)-1 & 0x3ffffff) << 38) | ((uint64_t)((uint32_t)-1 & 0x3ffffff) << 12) | ((uint32_t)-64 & 4095);
    u64(pos); var(23724); CHECK(mc_world_packet(&world,0x0a,data,length) == 0);
    CHECK(mc_world_get(&world,-1,-64,-1) == 23724);
    length = 0; u64(((uint64_t)0x3fffff << 42) | ((uint64_t)0x3fffff << 20) | 0xffffc); u8(0); var(1);
    uint64_t record = ((uint64_t)1 << 12) | (15 << 8) | (15 << 4); do { uint8_t b = record & 127; record >>= 7; u8(b | (record ? 128 : 0)); } while (record);
    CHECK(mc_world_packet(&world,0x43,data,length) == 0); CHECK(mc_world_get(&world,-1,-64,-1) == 1);
    length = 0; u32(-1); u32(-1); CHECK(mc_world_packet(&world,0x1e,data,length) == 0);
    CHECK(world.loaded == 0 && world.bytes == 0 && mc_world_get(&world,-1,-64,-1) == 0);
    mc_world_destroy(&world);
    McCamera camera = {0}; mc_camera_look(&camera,450,100); CHECK(camera.yaw == 90 && camera.pitch == 85);
    mc_camera_move(&camera,1,0,1,1); CHECK(camera.x < -4.99 && camera.y == 5);
    puts("World tests passed: registry, palettes, padding, truncation, negatives, updates, unload, camera");
    return 0;
}
