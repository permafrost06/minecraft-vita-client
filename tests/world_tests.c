#include "world.h"
#include "nbt.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Failed: %s at %d\n",#x,__LINE__); exit(1); } } while (0)
static uint8_t data[131072]; static size_t length,first_section;
static int registry_lighting;
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
    if(registry_lighting){name(1,"has_skylight");u8(0);name(5,"ambient_light");u32(0x3dcccccd);name(4,"fixed_time");u64(18000);}
    u8(0); u8(0); u8(0); u8(0); str("minecraft:overworld");
    str("minecraft:overworld");u64(12345);var(20);var(2);var(2);u8(0);u8(1);u8(0);u8(0);u8(0);
}
static void chunk(int x,int z,unsigned padding) {
    length = 0; u32(x); u32(z); name(10,""); u8(0); var(24*8+padding);
    first_section=length;
    for (int i = 0; i < 24; i++) {
        u16(i == 0 ? 4096 : 0); u8(0); var(i == 0 ? 1 : 0); var(0); u8(0); var(0); var(0);
    }
    for (unsigned i = 0; i < padding; i++) u8(0);
    var(0);u8(1);for(int i=0;i<6;i++)var(0);
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
static void light_data(uint64_t sky,uint64_t block,uint64_t empty_sky,uint64_t empty_block){
    u8(1);uint64_t masks[4]={sky,block,empty_sky,empty_block};
    for(int i=0;i<4;i++){var(1);u64(masks[i]);}
    for(int kind=0;kind<2;kind++){
        unsigned count=0;for(int s=0;s<64;s++)if(masks[kind]&(1ull<<s))count++;
        var(count);
        for(unsigned i=0;i<count;i++){var(2048);for(unsigned n=0;n<2048;n++)u8(kind?0x43:0x21);}
    }
}
static void lighting(void){
    McWorld w;mc_world_init(&w);registry();CHECK(mc_world_packet(&w,0x28,data,length)==0);
    chunk(0,0,0);CHECK(mc_world_packet(&w,0x24,data,length)==0);
    CHECK(mc_world_light(&w,0,-64,0)==-1);size_t before=w.bytes;
    length=0;var(0);var(0);light_data(2,2,0,0);
    for(size_t i=0;i<length;i++)CHECK(mc_world_packet(&w,0x27,data,i)<0);
    CHECK(mc_world_light(&w,0,-64,0)==-1 && w.bytes==before);
    CHECK(mc_world_packet(&w,0x27,data,length)==0);
    CHECK(mc_world_light(&w,0,-64,0)==0x31 && mc_world_light(&w,1,-64,0)==0x42);
    CHECK(mc_world_light(&w,0,-48,0)==-1 && w.bytes==before+416*256);
    CHECK(w.dirty[0]&3); /* updated section and its padded neighbor */
    length=0;var(0);var(0);light_data(0,0,2,0);CHECK(mc_world_packet(&w,0x27,data,length)==0);
    CHECK(mc_world_light(&w,0,-64,0)==0x30);
    length=0;var(0);var(0);light_data(0,0,0,2);CHECK(mc_world_packet(&w,0x27,data,length)==0);
    CHECK(mc_world_light(&w,0,-64,0)==0);
    length=0;var(0);var(0);light_data(2,0,2,0);CHECK(mc_world_packet(&w,0x27,data,length)<0);
    CHECK(mc_world_light(&w,0,-64,0)==0);
    length=0;var(0);var(0);light_data(1ull<<26,0,0,0);CHECK(mc_world_packet(&w,0x27,data,length)<0);
    chunk(0,0,0);length-=8;var(2);u8(0x12);u16(-60);var(1);u8(0);u8(0x23);u16(-60);var(2);name(10,"");u8(0);light_data(2,2,0,0);
    CHECK(mc_world_packet(&w,0x24,data,length)==0 && w.bytes==before+416*256+2*sizeof(McBlockEntity)+5);
    CHECK(mc_world_light(&w,0,-64,0)==0x31);
    length=0;u32(0);u32(0);CHECK(mc_world_packet(&w,0x1e,data,length)==0);
    CHECK(!w.loaded && !w.bytes && mc_world_light(&w,0,-64,0)==-1);
    mc_world_destroy(&w);
}
static void number(double d){uint64_t bits;memcpy(&bits,&d,8);u64(bits);}
static void borders(void){
    McWorld w;mc_world_init(&w);length=0;number(5);number(6);number(100);number(20);var(10000);var(29999984);var(5);var(15);
    CHECK(mc_world_packet(&w,0x22,data,length)==0 && w.border_x==5 && w.border_z==6);
    CHECK(fabs(mc_world_border_size(&w,w.border_started+5000)-60)<.001);
    CHECK(mc_world_border_size(&w,w.border_started+10000)==20);
    uint64_t started=w.border_started;length=0;number(7);number(8);
    CHECK(mc_world_packet(&w,0x47,data,length)==0 && w.border_started==started);
    length=0;number(20);number(40);var(2000);CHECK(mc_world_packet(&w,0x48,data,length)==0);
    CHECK(mc_world_border_size(&w,w.border_started+1000)==30);
    length=0;number(12);CHECK(mc_world_packet(&w,0x49,data,length)==0 && mc_world_border_size(&w,0)==12);
    length=0;var(8);CHECK(mc_world_packet(&w,0x4a,data,length)==0 && w.border_warning_time==8);
    CHECK(mc_world_packet(&w,0x4b,data,length)==0 && w.border_warning_blocks==8);
    length=0;number(NAN);CHECK(mc_world_packet(&w,0x49,data,length)<0 && w.border_new==12);
    mc_world_destroy(&w);
}
static void environment_tests(void){
    McWorld w;mc_world_init(&w);
    length=0;u64(123);u64(6000);CHECK(mc_world_packet(&w,0x5e,data,length)==0);
    uint64_t received=w.environment.received;
    CHECK(fabs(mc_environment_time(&w.environment,received+1000)-6020)<.001);
    McLighting noon=mc_environment_lighting(&w.environment,received);
    CHECK(noon.sky>.99f);
    length=0;u64(123);u64((uint64_t)(int64_t)-18000);CHECK(mc_world_packet(&w,0x5e,data,length)==0);
    CHECK(mc_environment_time(&w.environment,received+100000)==18000);
    McLighting night=mc_environment_lighting(&w.environment,received);
    CHECK(night.sky<.21f && night.horizon<noon.horizon);
    CHECK(mc_light_brightness(1,0,&night)<mc_light_brightness(1,0,&noon)*.3f);
    CHECK(mc_light_brightness(0,1,&night)==mc_light_brightness(0,1,&noon));
    length=0;u8(7);uint32_t f=0x3f800000;u32(f);CHECK(mc_world_packet(&w,0x1f,data,length)==0);
    u8(0);CHECK(mc_world_packet(&w,0x1f,data,length)<0 && w.environment.rain==1);length--;
    data[1]=0x7f;data[2]=0xc0;CHECK(mc_world_packet(&w,0x1f,data,length)<0 && w.environment.rain==1);
    length=0;u8(8);u32(f);CHECK(mc_world_packet(&w,0x1f,data,length)==0);
    McLighting storm=mc_environment_lighting(&w.environment,received);CHECK(storm.sky<night.sky);
    w.environment.has_sky=0;w.environment.ambient=.1f;
    McLighting nether=mc_environment_lighting(&w.environment,received);CHECK(nether.sky==0 && nether.ambient==.1f);
    mc_world_clear(&w);CHECK(w.environment.has_sky && !w.environment.known && !w.environment.rain);
    registry_lighting=1;registry();CHECK(mc_world_packet(&w,0x28,data,length)==0);registry_lighting=0;
    CHECK(!w.environment.has_sky && w.environment.fixed && fabs(w.environment.ambient-.1)<.0001);
    length=0;u64(123);u64(6000);CHECK(mc_world_packet(&w,0x5e,data,length)==0);
    CHECK(mc_environment_time(&w.environment,w.environment.received+10000)==18000);
    mc_world_destroy(&w);
}
static void position(int x,int y,int z){u64(((uint64_t)(x&0x3ffffff)<<38)|((uint64_t)(z&0x3ffffff)<<12)|(y&4095));}
static void block_entities(void){
    McWorld w;mc_world_init(&w);registry();CHECK(mc_world_packet(&w,0x28,data,length)==0);
    chunk(-1,-2,0);length-=8;var(1);u8(0xff);u16(300);var(7);
    name(10,"");nstr("Text1","{\"text\":\"Hello\",\"color\":\"red\",\"extra\":[{\"text\":\" world\"}]}");nstr("Text2","\"Line 2\"");u8(0);
    u8(1);for(int i=0;i<6;i++)var(0);
    CHECK(mc_world_packet(&w,0x24,data,length)==0);
    const McBlockEntity *e=mc_world_block_entity(&w,-1,300,-17);CHECK(e && e->type==7 && e->pos>65535);
    char line[128];CHECK(mc_block_entity_sign_line(e,0,line,sizeof(line))==0 && !strcmp(line,"Hello world"));
    CHECK(mc_block_entity_sign_line(e,1,line,5)==0 && !strcmp(line,"Line"));
    CHECK(mc_block_entity_sign_line(e,4,line,sizeof(line))<0);
    size_t before=w.bytes;uint64_t revision=w.revision;
    length=0;position(-1,300,-17);var(7);name(10,"");nstr("Text1","{\"text\":\"Updated\"}");u8(0);
    for(size_t i=0;i<length;i++)CHECK(mc_world_packet(&w,0x08,data,i)<0);
    CHECK(w.bytes==before && w.revision==revision);
    CHECK(mc_world_packet(&w,0x08,data,length)==0);
    e=mc_world_block_entity(&w,-1,300,-17);CHECK(mc_block_entity_sign_line(e,0,line,sizeof(line))==0 && !strcmp(line,"Updated"));
    CHECK(w.block_entity_updates==1);
    before=w.bytes;revision=w.revision;
    length=0;position(-1,300,-17);var(7);name(10,"");
    static char oversized[MC_BLOCK_ENTITY_NBT];memset(oversized,'x',sizeof(oversized)-1);
    nstr("Text1",oversized);u8(0);CHECK(mc_world_packet(&w,0x08,data,length)<0);
    CHECK(w.bytes==before && w.revision==revision);
    chunk(-1,-2,0);length-=8;var(MC_BLOCK_ENTITIES+1);CHECK(mc_world_packet(&w,0x24,data,length)<0);
    CHECK(w.bytes==before && mc_world_block_entity(&w,-1,300,-17));

    chunk(-1,-2,0);length-=8;var(2);
    for(int i=0;i<2;i++){u8(0xff);u16(300);var(7);u8(0);}u8(1);for(int i=0;i<6;i++)var(0);
    CHECK(mc_world_packet(&w,0x24,data,length)<0 && mc_world_block_entity(&w,-1,300,-17));
    CHECK(mc_world_set(&w,-1,300,-17,1)==0 && !mc_world_block_entity(&w,-1,300,-17));
    CHECK(mc_world_set(&w,-1,300,-17,2950)==0);
    length=0;position(-1,300,-17);u8(1);u8(2);var(176);CHECK(mc_world_packet(&w,0x09,data,length)==0);
    e=mc_world_block_entity(&w,-1,300,-17);CHECK(e && e->action==1 && e->parameter==2 && e->type==1);
    CHECK(mc_world_set(&w,-1,300,-17,2951)==0);
    CHECK(mc_world_block_entity(&w,-1,300,-17)); /* Waterlogged state keeps NBT/action. */
    length=0;position(-1,300,-17);var(1);u8(0);CHECK(mc_world_packet(&w,0x08,data,length)==0);
    CHECK(mc_world_block_entity(&w,-1,300,-17)->parameter==2);
    CHECK(mc_world_block_entity(&w,-1,300,-17));
    length=0;u32(-1);u32(-2);CHECK(mc_world_packet(&w,0x1e,data,length)==0 && !w.bytes);
    CHECK(!mc_world_block_entity(&w,-1,300,-17));mc_world_destroy(&w);
}
int main(void) {
    environment_tests();block_entities();
    borders();lighting();
    palettes(); McWorld world; mc_world_init(&world);
    registry(); CHECK(mc_world_packet(&world,0x28,data,length) == 0);
    CHECK(world.ready && world.height == 384 && world.min_y == -64 && world.view_distance==2);
    for (size_t i = 0; i < length; i++) {
        McWorld invalid; mc_world_init(&invalid); CHECK(mc_world_packet(&invalid,0x28,data,i) < 0); mc_world_destroy(&invalid);
    }
    chunk(-1,-1,16); CHECK(mc_world_packet(&world,0x24,data,length) == 0);
    CHECK(world.loaded == 1 && world.bytes == 384*256*2);
    /* Reproduce vanilla chunk 25,-30: its section count hint was 4107.
       It does not control the number of packed block values (always 4096). */
    data[first_section]=4107 >> 8;data[first_section+1]=4107 & 255;
    CHECK(mc_world_packet(&world,0x24,data,length)==0);
    CHECK(mc_world_get(&world,-1,-64,-1)==1);
    data[first_section]=8192 >> 8;data[first_section+1]=8192 & 255;
    CHECK(mc_world_packet(&world,0x24,data,length)==0);

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
    chunk(0,0,0); CHECK(mc_world_packet(&world,0x24,data,length) == 0);
    struct { uint32_t before; uint32_t pixels[MC_VIEW_W*MC_VIEW_H]; uint32_t after; } *image = malloc(sizeof(*image));
    CHECK(image != NULL); image->before = image->after = 0x1234abcd;
    McCamera surface = {8,-47,8,0,70}; mc_render_init();
    mc_render_preview(&world,&surface,image->pixels,2);
    CHECK(image->before == 0x1234abcd && image->after == 0x1234abcd);
    CHECK(image->pixels[100*MC_VIEW_W+120] != 0xfff0d0b4); /* ground, rather than sky */
    mc_render(&world,&surface,image->pixels);
    CHECK(image->before == 0x1234abcd && image->after == 0x1234abcd);
    /* A wall west of a south-facing camera belongs on screen right. */
    chunk(-1,0,0); CHECK(mc_world_packet(&world,0x24,data,length) == 0);
    for(int y=-43;y<=-38;y++)for(int z=1;z<=10;z++)mc_world_set(&world,-2,y,z,1);
    McCamera south={0,-40,0,0,0};mc_render(&world,&south,image->pixels);
    CHECK(image->pixels[(MC_VIEW_H/2)*MC_VIEW_W+MC_VIEW_W*3/4] != 0xfff0d0b4);
    CHECK(image->pixels[(MC_VIEW_H/2)*MC_VIEW_W+MC_VIEW_W/4] == 0xfff0d0b4);
    free(image); mc_world_destroy(&world);
    /* Sliding cache: arriving chunks may precede old unloads. Never let a
       delayed unload erase a newer column that reused the same hash slot. */
    mc_world_init(&world);registry();CHECK(mc_world_packet(&world,0x28,data,length)==0);
    for(int z=-4;z<=4;z++)for(int x=-4;x<=4;x++){chunk(x,z,0);CHECK(mc_world_packet(&world,0x24,data,length)==0);}
    for(int center=1;center<=12;center++){
        length=0;var(center);var(0);CHECK(mc_world_packet(&world,0x4e,data,length)==0);
        for(int z=-4;z<=4;z++){
            chunk(center+4,z,0);CHECK(mc_world_packet(&world,0x24,data,length)==0);
            length=0;u32(center-5);u32(z);CHECK(mc_world_packet(&world,0x1e,data,length)==0);
            CHECK(mc_world_chunk(&world,center+4,z)!=NULL);
        }
        CHECK(world.loaded==MC_CACHE_COUNT && world.bytes==MC_CACHE_COUNT*384u*256u*2u);
        for(int z=-4;z<=4;z++)for(int x=center-4;x<=center+4;x++)CHECK(mc_world_chunk(&world,x,z)!=NULL);
    }
    /* A server view distance larger than the ring must retain all nearby slots. */
    uint64_t before_far=world.evictions;
    for(int z=-12;z<=12;z++)for(int x=0;x<=24;x++)if(abs(x-12)>4 || abs(z)>4){chunk(x,z,0);CHECK(mc_world_packet(&world,0x24,data,length)==0);}
    for(int z=-4;z<=4;z++)for(int x=8;x<=16;x++)CHECK(mc_world_chunk(&world,x,z)!=NULL);
    CHECK(world.evictions==before_far && world.loaded==81);
    CHECK(world.decoded==81+12*9+(25*25-81) && world.center_x==12 && world.center_updates==12);
    for(int center=11;center>=-12;center--){
        length=0;var((uint32_t)center);var(0);CHECK(mc_world_packet(&world,0x4e,data,length)==0);
        for(int z=-4;z<=4;z++){
            chunk(center-4,z,0);CHECK(mc_world_packet(&world,0x24,data,length)==0);
            length=0;u32(center+5);u32(z);CHECK(mc_world_packet(&world,0x1e,data,length)==0);
            CHECK(mc_world_chunk(&world,center-4,z)!=NULL);
        }
        for(int z=-4;z<=4;z++)for(int x=center-4;x<=center+4;x++)CHECK(mc_world_chunk(&world,x,z)!=NULL);
    }
    CHECK(world.center_x==-12 && world.loaded==81);mc_world_destroy(&world);
    CHECK(mc_camera_axis(111) == 0 && mc_camera_axis(137) == 0);
    CHECK(mc_camera_axis(128) == 0 && mc_camera_axis(0) == -1 && mc_camera_axis(255) == 1);
    CHECK(mc_camera_axis(180) > 0 && mc_camera_axis(64) < 0);
    McCamera camera = {0}; mc_camera_look(&camera,450,100); CHECK(camera.yaw == 90 && camera.pitch == 85);
    mc_camera_move(&camera,1,0,1,1); CHECK(camera.x < -4.99 && camera.y == 5);
    puts("World tests passed: registry, palettes, padding, truncation, negatives, updates, unload, camera");
    return 0;
}
