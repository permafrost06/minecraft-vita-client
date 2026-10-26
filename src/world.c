#include "world.h"
#include "nbt.h"
#include "net.h"
#include "game_data.h"
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int bad(McWorld *w, const char *message) { snprintf(w->error,sizeof(w->error),"%s",message); return -1; }
void mc_world_init(McWorld *w) { memset(w,0,sizeof(*w));mc_environment_init(&w->environment);w->border_old=w->border_new=59999968;w->border_warning_blocks=5;w->border_warning_time=15; }
static void free_entities(McChunk *c){
    for(unsigned i=0;i<c->entity_count;i++)free(c->entities[i].nbt);
    free(c->entities);c->entities=NULL;c->entity_count=0;c->entity_bytes=0;
}
void mc_world_clear(McWorld *w) {
    for (int i = 0; i < MC_CACHE_COUNT; i++) { free_entities(&w->chunks[i]);free(w->chunks[i].light);w->chunks[i].light=NULL;memset(w->chunks[i].light_known,0,sizeof(w->chunks[i].light_known));free(w->chunks[i].blocks); w->chunks[i].blocks = NULL; }
    mc_environment_init(&w->environment);w->loaded = 0; w->bytes = 0; w->revision++;
}
void mc_world_destroy(McWorld *w) { mc_world_clear(w); }
static unsigned slot(int x,int z) {
    int a = x % MC_CACHE_SIDE, b = z % MC_CACHE_SIDE;
    if (a < 0) a += MC_CACHE_SIDE;
    if (b < 0) b += MC_CACHE_SIDE;
    return b*MC_CACHE_SIDE+a;
}
static int floor16(int n) { int q = n/16; return q-(n%16 < 0); }
const McChunk *mc_world_chunk(const McWorld *w,int x,int z) {
    const McChunk *c = &w->chunks[slot(x,z)];
    return c->blocks && c->x == x && c->z == z ? c : NULL;
}
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
    unsigned index=(y-w->min_y)*256+((unsigned)z&15)*16+((unsigned)x&15);
    if(c->blocks[index]!=state && mc_dig_def(c->blocks[index])!=mc_dig_def(state))for(unsigned i=0;i<c->entity_count;i++)if(c->entities[i].pos==index){
        size_t bytes=sizeof(McBlockEntity)+c->entities[i].size;
        free(c->entities[i].nbt);c->entities[i]=c->entities[--c->entity_count];c->entity_bytes-=bytes;w->bytes-=bytes;break;
    }
    c->blocks[index] = state;
    /* Mark the section and any neighbor whose border faces or occlusion depend on it. */
    int section = (y-w->min_y)/16, ly = (y-w->min_y)%16, lx = x & 15, lz = z & 15;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        if ((dx < 0 && lx != 0) || (dx > 0 && lx != 15) || (dz < 0 && lz != 0) || (dz > 0 && lz != 15)) continue;
        uint32_t bits = 1u << section;
        if (ly == 0 && section > 0) bits |= 1u << (section-1);
        if (ly == 15 && section < 31) bits |= 1u << (section+1);
        w->dirty[slot(cx+dx,cz+dz)] |= bits;
    }
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
            mc_world_clear(w);w->environment.has_sky=d->has_sky;w->environment.ambient=d->ambient;w->environment.fixed=d->fixed;if(d->fixed)w->environment.time=d->fixed_time; w->min_y = d->min_y; w->height = d->height; w->ready = 1;
            snprintf(w->dimension,sizeof(w->dimension),"%s",name); return 0;
        }
    }
    return bad(w,"Dimension is missing from the server registry");
}
static void biome_registry(McWorld *w,McNbt codec);
static int join(McWorld *w,McReader *r) {
    mc_skip(r,7); unsigned worlds = mc_varint_read(r);
    if (r->error || worlds > 1024) return bad(w,"Invalid world-name list");
    for (unsigned i = 0; i < worlds; i++) { unsigned n = mc_varint_read(r); mc_skip(r,n); }
    McNbt codec,registry,list;
    if (mc_nbt_root(r,&codec) < 0 || mc_nbt_find(codec,"minecraft:dimension_type",&registry) < 0 ||
        mc_nbt_find(registry,"value",&list) < 0 || list.type != 9) return bad(w,"Invalid dimension registry NBT");
    biome_registry(w,codec);
    McReader a = {list.data,list.size,0,0}; unsigned type = mc_u8(&a), count = mc_u32(&a);
    if (type != 10 || !count || count > MC_MAX_DIMENSIONS) return bad(w,"Too many server dimensions");
    McDimension dimensions[MC_MAX_DIMENSIONS]={0};
    for (unsigned i = 0; i < count; i++) {
        McNbt entry,name,element,min_y,height;
        if (mc_nbt_payload(&a,type,0,&entry) < 0 || mc_nbt_find(entry,"name",&name) < 0 ||
            mc_nbt_string(name,dimensions[i].name,sizeof(dimensions[i].name)) < 0 ||
            mc_nbt_find(entry,"element",&element) < 0 || mc_nbt_find(element,"min_y",&min_y) < 0 ||
            mc_nbt_find(element,"height",&height) < 0 || mc_nbt_int(min_y,&dimensions[i].min_y) < 0 ||
            mc_nbt_int(height,&dimensions[i].height) < 0) return bad(w,"Invalid dimension bounds");
        McNbt field;dimensions[i].has_sky=1;
        if(mc_nbt_find(element,"has_skylight",&field)==0){
            if(field.type!=1 || field.size!=1 || field.data[0]>1)return bad(w,"Invalid dimension skylight");
            dimensions[i].has_sky=field.data[0];
        }
        if(mc_nbt_find(element,"ambient_light",&field)==0){
            McReader f={field.data,field.size,0,0};float value=mc_f32(&f);
            if(field.type!=5 || f.error || !isfinite(value) || value<0 || value>1)return bad(w,"Invalid ambient light");
            dimensions[i].ambient=value;
        }
        if(mc_nbt_find(element,"fixed_time",&field)==0){
            if(field.type!=4 || field.size!=8)return bad(w,"Invalid fixed time");
            McReader f={field.data,field.size,0,0};dimensions[i].fixed_time=mc_u64(&f);dimensions[i].fixed=1;
        }

    }
    char dimension[128];
    if (mc_string(r,dimension,sizeof(dimension)) < 0) return bad(w,"Invalid active dimension");
    unsigned world_name=mc_varint_read(r);mc_skip(r,world_name);mc_skip(r,8);
    mc_varint_read(r);unsigned distance=mc_varint_read(r),simulation=mc_varint_read(r);
    for(unsigned i=0;i<4;i++)if(mc_u8(r)>1)r->error=1;
    unsigned death=mc_u8(r);
    if(death>1)r->error=1;
    if(death==1){unsigned name=mc_varint_read(r);mc_skip(r,name);mc_skip(r,8);}
    if(r->error || r->pos!=r->n || distance>64 || simulation>64)return bad(w,"Invalid world streaming settings");
    memcpy(w->dimensions,dimensions,count*sizeof(*dimensions)); w->dimension_count = count;
    if(select_dimension(w,dimension)<0)return -1;
    w->view_distance=distance;return 0;
}
#define LIGHT_SECTIONS (MC_MAX_HEIGHT/16+2)
typedef struct {uint64_t mask[2],empty[2];const uint8_t *array[2][LIGHT_SECTIONS];} LightPatch;
static uint64_t light_mask(McReader *r,unsigned sections){
    unsigned words=mc_varint_read(r);if(words>1){r->error=1;return 0;}
    uint64_t bits=words?mc_u64(r):0;if(bits>>sections)r->error=1;return bits;
}
static int read_light(McReader *r,int height,LightPatch *patch){
    memset(patch,0,sizeof(*patch));unsigned sections=height/16+2;
    if(mc_u8(r)>1)r->error=1; /* trustEdges */
    for(int kind=0;kind<2;kind++)patch->mask[kind]=light_mask(r,sections);
    for(int kind=0;kind<2;kind++)patch->empty[kind]=light_mask(r,sections);
    for(int kind=0;kind<2;kind++){
        if(patch->mask[kind]&patch->empty[kind])r->error=1;
        unsigned count=mc_varint_read(r),expected=0;
        for(unsigned s=0;s<sections;s++)if(patch->mask[kind]&(1ull<<s))expected++;
        if(count!=expected)r->error=1;
        for(unsigned s=0;s<sections && !r->error;s++)if(patch->mask[kind]&(1ull<<s)){
            if(mc_varint_read(r)!=2048 || r->n-r->pos<2048){r->error=1;break;}
            patch->array[kind][s]=r->p+r->pos;mc_skip(r,2048);
        }
    }
    return r->error || r->pos!=r->n?-1:0;
}
static int apply_light(McWorld *w,McChunk *c,const LightPatch *p,int account){
    unsigned sections=w->height/16+2;size_t bytes=(size_t)sections*4096;
    if(!(p->mask[0]|p->mask[1]|p->empty[0]|p->empty[1]))return 0;
    if(!c->light){c->light=calloc(bytes,1);if(!c->light)return bad(w,"Light cache allocation failed");if(account)w->bytes+=bytes;}
    uint32_t dirty=0;
    for(int kind=0;kind<2;kind++){
        uint64_t changed=p->mask[kind]|p->empty[kind];c->light_known[kind]|=changed;
        for(unsigned s=0;s<sections;s++)if(changed&(1ull<<s)){
            uint8_t *out=c->light+s*4096;const uint8_t *in=p->array[kind][s];unsigned shift=kind*4,keep=kind?15:240;
            for(unsigned i=0;i<4096;i++){unsigned value=in?(in[i/2]>>((i&1)*4))&15:0;out[i]=(out[i]&keep)|(value<<shift);}
            /* Padded meshing samples adjacent sections and columns. */
            for(int n=(int)s-2;n<=(int)s;n++)if(n>=0 && n<w->height/16)dirty|=1u<<n;
        }
    }
    if(account){
        for(int z=-1;z<=1;z++)for(int x=-1;x<=1;x++)w->dirty[slot(c->x+x,c->z+z)]|=dirty;
        w->revision++;w->light_updates++;
    }
    return 0;
}
int mc_world_light(const McWorld *w,int x,int y,int z){
    const McChunk *c=mc_world_chunk(w,floor16(x),floor16(z));int ly=y-w->min_y+16;
    if(!c || !c->light || ly<0 || ly>=w->height+32)return -1;
    uint64_t bit=1ull<<(ly/16);
    if(!(c->light_known[0]&bit) && !(c->light_known[1]&bit))return -1;
    return c->light[ly*256+((unsigned)z&15)*16+((unsigned)x&15)];
}
McNbt mc_block_entity_nbt(const McBlockEntity *e){
    McNbt n={0};if(e && e->size){McReader r={e->nbt,e->size,0,0};mc_nbt_root(&r,&n);}return n;
}
const McBlockEntity *mc_world_block_entity(const McWorld *w,int x,int y,int z){
    const McChunk *c=mc_world_chunk(w,floor16(x),floor16(z));
    if(!c || y<w->min_y || y>=w->min_y+w->height)return NULL;
    unsigned pos=((y-w->min_y)<<8)|((z&15)<<4)|(x&15);
    for(unsigned i=0;i<c->entity_count;i++)if(c->entities[i].pos==pos)return c->entities+i;
    return NULL;
}
static int entity_nbt(McReader *r,McBlockEntity *e){
    size_t start=r->pos;McNbt n;
    if(r->pos<r->n && !r->p[r->pos])mc_u8(r);
    else if(mc_nbt_root(r,&n)<0)return -1;
    size_t size=r->pos-start;
    if(r->error || size>MC_BLOCK_ENTITY_NBT)return -1;
    e->nbt=malloc(size);if(!e->nbt)return -1;
    memcpy(e->nbt,r->p+start,size);e->size=size;return 0;
}
static int read_block_entities(McReader *r,McWorld *w,McChunk *c){
    unsigned count=mc_varint_read(r);if(r->error || count>MC_BLOCK_ENTITIES)return -1;
    if(count){c->entities=calloc(count,sizeof(*c->entities));if(!c->entities)return -1;}
    for(unsigned i=0;i<count;i++){
        McBlockEntity *e=c->entities+i;c->entity_count++;
        unsigned xz=mc_u8(r);int y=(int16_t)mc_u16(r);e->type=mc_varint_read(r);
        if(r->error || y<w->min_y || y>=w->min_y+w->height || e->type>INT32_MAX)return -1;
        e->pos=((y-w->min_y)<<8)|((xz&15)<<4)|(xz>>4);
        for(unsigned j=0;j<i;j++)if(c->entities[j].pos==e->pos)return -1;
        if(entity_nbt(r,e)<0)return -1;
        c->entity_bytes+=sizeof(*e)+e->size;
        if(c->entity_bytes>MC_BLOCK_ENTITY_BYTES)return -1;
    }
    return r->error?-1:0;
}
static int chunk(McWorld *w,McReader *r) {
    if (!w->ready) return bad(w,"Chunk received before dimension registry");
    int x = (int32_t)mc_u32(r), z = (int32_t)mc_u32(r); McNbt heightmaps;
    if (mc_nbt_root(r,&heightmaps) < 0) return bad(w,"Invalid chunk heightmap NBT");
    unsigned length = mc_varint_read(r);
    if (r->error || length > r->n-r->pos) return bad(w,"Truncated chunk sections");
    McReader sections = {r->p+r->pos,length,0,0};
    /* Biome ids (one byte per 4x4x4 cell) live after the block states. */
    uint16_t *blocks = malloc((size_t)w->height*256*sizeof(uint16_t)+(size_t)(w->height/16)*64);
    if (!blocks) return bad(w,"Chunk cache allocation failed");
    uint8_t *biomes=(uint8_t *)(blocks+(size_t)w->height*256);uint16_t cells[64];
    for (int y = 0; y < w->height/16; y++) {
        /* This is a population hint, not an array length. Vanilla sections can
           report values above 4096; storage is bounded by the palette decoder. */
        mc_u16(&sections);
        size_t start=sections.pos;unsigned bits=start<sections.n?sections.p[start]:255;
        if(mc_palette(&sections,4096,0,blocks+y*4096)<0){
            free(blocks);snprintf(w->error,sizeof(w->error),"Chunk %d,%d section %d: block bits=%u at=%u",x,z,y,bits,(unsigned)start);return -1;
        }
        start=sections.pos;bits=start<sections.n?sections.p[start]:255;
        if(mc_palette(&sections,64,1,cells)<0){
            free(blocks);snprintf(w->error,sizeof(w->error),"Chunk %d,%d section %d: biome bits=%u at=%u",x,z,y,bits,(unsigned)start);return -1;
        }
        for(int i=0;i<64;i++)biomes[y*64+i]=cells[i]>255?255:(uint8_t)cells[i];
    }
    /* Vanilla can over-allocate its section byte array and leave zero padding.
       Read exactly the dimension's sections; permit only bounded zero padding. */
    if (sections.n-sections.pos > (size_t)(w->height/16)*8) sections.error = 1;
    while (sections.pos < sections.n) if (mc_u8(&sections) != 0) sections.error = 1;
    if (sections.error) { free(blocks); return bad(w,"Invalid trailing chunk section bytes"); }
    r->pos+=length;LightPatch lights;
    McChunk incoming={.x=x,.z=z,.blocks=blocks};
    if(read_block_entities(r,w,&incoming)<0 || read_light(r,w->height,&lights)<0){free_entities(&incoming);free(blocks);return bad(w,"Invalid chunk light or block entity data (cache limits apply)");}
    if(apply_light(w,&incoming,&lights,0)<0){free_entities(&incoming);free(blocks);return -1;}
    /* Servers can send beyond our bounded cache even after client settings.
       A distant column sharing a ring slot must not evict nearby terrain. */
    if(w->center_updates && (llabs((long long)x-w->center_x)>MC_CACHE_SIDE/2 || llabs((long long)z-w->center_z)>MC_CACHE_SIDE/2)){
        free_entities(&incoming);free(incoming.light);free(incoming.blocks);w->decoded++;return 0;
    }
    McChunk *c = &w->chunks[slot(x,z)]; size_t bytes = (size_t)w->height*256*sizeof(uint16_t);
    if (c->blocks) { if(c->x!=x || c->z!=z)w->evictions++;w->bytes-=c->entity_bytes;free_entities(c);free(c->blocks);if(c->light){free(c->light);w->bytes-=(size_t)(w->height+32)*256;} } else { w->loaded++; w->bytes += bytes; }
    w->bytes+=incoming.entity_bytes;
    if(incoming.light)w->bytes+=(size_t)(w->height+32)*256;
    static uint64_t serial; w->decoded++;incoming.serial=++serial;*c=incoming; w->revision++;w->last_chunk_x=x;w->last_chunk_z=z; return 0;
}
static int sign_bits(uint64_t n,unsigned bits) { uint64_t mask = 1ull << (bits-1); return (int)((int64_t)(n ^ mask)-(int64_t)mask); }
double mc_world_border_size(const McWorld *w,uint64_t now){
    if(!w->border_duration || (now>=w->border_started && now-w->border_started>=w->border_duration))return w->border_new;
    double t=now>w->border_started?(double)(now-w->border_started)/w->border_duration:0;
    return w->border_old+(w->border_new-w->border_old)*t;
}
static int border_packet(McWorld *w,uint32_t id,McReader *r){
    double x=w->border_x,z=w->border_z,old=w->border_old,new_size=w->border_new;
    uint64_t duration=w->border_duration;int warning_blocks=w->border_warning_blocks,warning_time=w->border_warning_time;
    if(id==0x22){x=mc_f64(r);z=mc_f64(r);old=mc_f64(r);new_size=mc_f64(r);duration=mc_varlong(r);mc_varint_read(r);warning_blocks=mc_varint_read(r);warning_time=mc_varint_read(r);}
    else if(id==0x47){x=mc_f64(r);z=mc_f64(r);}
    else if(id==0x48){old=mc_f64(r);new_size=mc_f64(r);duration=mc_varlong(r);}
    else if(id==0x49){old=new_size=mc_f64(r);duration=0;}
    else if(id==0x4a)warning_time=mc_varint_read(r);
    else if(id==0x4b)warning_blocks=mc_varint_read(r);
    if(r->error || r->pos!=r->n || !isfinite(x) || !isfinite(z) || !isfinite(old) || !isfinite(new_size) || fabs(x)>30000000 || fabs(z)>30000000 || old<0 || new_size<0 || old>60000000 || new_size>60000000 || duration>INT64_MAX || warning_blocks<0 || warning_time<0)return bad(w,"Invalid world border");
    w->border_x=x;w->border_z=z;w->border_old=old;w->border_new=new_size;w->border_duration=duration;w->border_warning_blocks=warning_blocks;w->border_warning_time=warning_time;
    if(id==0x22 || id==0x48 || id==0x49)w->border_started=net_millis();
    return 0;
}
int mc_world_biome(const McWorld *w,int x,int y,int z){
    if(!w->ready || y<w->min_y || y>=w->min_y+w->height)return -1;
    int cx=floor16(x),cz=floor16(z);const McChunk *c=&w->chunks[slot(cx,cz)];
    if(!c->blocks || c->x!=cx || c->z!=cz)return -1;
    const uint8_t *biomes=(const uint8_t *)(c->blocks+(size_t)w->height*256);int ly=y-w->min_y;
    return biomes[(ly/16)*64+((ly&15)/4)*16+((z&15)/4)*4+(x&15)/4];
}
/* Barycentric blend over the (hot-wet, hot-dry, cold) corners of the climate triangle. */
static uint32_t climate(float temperature,float downfall,const uint32_t corner[3]){
    float t=temperature<0?0:temperature>1?1:temperature,r=(downfall<0?0:downfall>1?1:downfall)*t;
    float weight[3]={r,t-r,1-t};uint32_t out=0;
    for(int shift=0;shift<24;shift+=8){float v=0;for(int i=0;i<3;i++)v+=weight[i]*((corner[i]>>shift)&255);out|=(uint32_t)(v+.5f)<<shift;}
    return out;
}
uint32_t mc_biome_grass_color(float temperature,float downfall){static const uint32_t c[3]={0x50c53a,0xbfb755,0x80b497};return climate(temperature,downfall,c);}
uint32_t mc_biome_foliage_color(float temperature,float downfall){static const uint32_t c[3]={0x30bb0b,0xaea42a,0x60a17b};return climate(temperature,downfall,c);}
static int nbt_number(McNbt node,float *out){
    McReader r={node.data,node.size,0,0};
    if(node.type==5){uint32_t bits=mc_u32(&r);memcpy(out,&bits,4);}
    else if(node.type==3)*out=(float)(int32_t)mc_u32(&r);
    else if(node.type==6){uint64_t bits=mc_u64(&r);double d;memcpy(&d,&bits,8);*out=(float)d;}
    else return -1;
    return r.error?-1:0;
}
static void biome_registry(McWorld *w,McNbt codec){
    McNbt registry,list;w->biome_count=0;w->plains_biome=-1;
    if(mc_nbt_find(codec,"minecraft:worldgen/biome",&registry)<0 || mc_nbt_find(registry,"value",&list)<0 || list.type!=9)return;
    McReader a={list.data,list.size,0,0};unsigned type=mc_u8(&a);uint32_t count=mc_u32(&a);if(type!=10)return;
    for(uint32_t i=0;i<count && !a.error;i++){
        McNbt entry,name,id,element,effects,field;char key[64];int index;float temperature=.8f,downfall=.4f;
        if(mc_nbt_payload(&a,10,0,&entry)<0)break;
        if(mc_nbt_find(entry,"id",&id)<0 || mc_nbt_int(id,&index)<0 || index<0 || index>=MC_MAX_BIOMES || mc_nbt_find(entry,"element",&element)<0)continue;
        if(mc_nbt_find(element,"temperature",&field)==0)nbt_number(field,&temperature);
        if(mc_nbt_find(element,"downfall",&field)==0)nbt_number(field,&downfall);
        uint32_t grass=mc_biome_grass_color(temperature,downfall),foliage=mc_biome_foliage_color(temperature,downfall),water=0x3f76e4;int color;
        if(mc_nbt_find(element,"effects",&effects)==0){
            if(mc_nbt_find(effects,"water_color",&field)==0 && mc_nbt_int(field,&color)==0)water=(uint32_t)color&0xffffff;
            if(mc_nbt_find(effects,"foliage_color",&field)==0 && mc_nbt_int(field,&color)==0)foliage=(uint32_t)color&0xffffff;
            if(mc_nbt_find(effects,"grass_color",&field)==0 && mc_nbt_int(field,&color)==0)grass=(uint32_t)color&0xffffff;
            char modifier[32];
            if(mc_nbt_find(effects,"grass_color_modifier",&field)==0 && mc_nbt_string(field,modifier,sizeof(modifier))==0){
                if(!strcmp(modifier,"swamp"))grass=0x6a7039;
                else if(!strcmp(modifier,"dark_forest"))grass=((grass&0xfefefe)+0x28340a)>>1;
            }
        }
        w->biome_grass[index]=grass;w->biome_foliage[index]=foliage;w->biome_water[index]=water;
        if(index+1>w->biome_count)w->biome_count=index+1;
        if(mc_nbt_find(entry,"name",&name)==0 && mc_nbt_string(name,key,sizeof(key))==0 && !strcmp(key,"minecraft:plains"))w->plains_biome=index;
    }
}
/* Chunk Biomes: replaces the biome cells of loaded columns and requests remeshing. */
static int chunk_biomes(McWorld *w,McReader *r){
    uint32_t count=mc_varint_read(r);if(r->error || count>4096)return bad(w,"Invalid chunk biomes");
    for(uint32_t i=0;i<count;i++){
        uint64_t packed=mc_u64(r);int cx=(int32_t)(uint32_t)packed,cz=(int32_t)(packed>>32);uint32_t length=mc_varint_read(r);
        if(r->error || length>r->n-r->pos)return bad(w,"Invalid chunk biomes");
        McReader data={r->p+r->pos,length,0,0};r->pos+=length;
        McChunk *c=&w->chunks[slot(cx,cz)];int present=c->blocks && c->x==cx && c->z==cz;
        uint8_t *biomes=present?(uint8_t *)(c->blocks+(size_t)w->height*256):NULL;
        for(int y=0;y<w->height/16;y++){uint16_t cells[64];
            if(mc_palette(&data,64,1,cells)<0)return bad(w,"Invalid chunk biome palette");
            if(biomes)for(int k=0;k<64;k++)biomes[y*64+k]=cells[k]>255?255:(uint8_t)cells[k];}
        if(present){w->dirty[slot(cx,cz)]=0xffffffffu;w->biome_updates++;w->revision++;}
    }
    return r->pos==r->n?0:bad(w,"Invalid chunk biomes");
}
int mc_world_packet(McWorld *w,uint32_t id,const uint8_t *p,size_t n) {
    McReader r = {p,n,0,0};
    if(id==0x5e){
        mc_u64(&r);uint64_t raw=mc_u64(&r);
        if(r.error || r.pos!=n)return bad(w,"Invalid world time");
        if(!w->environment.fixed)w->environment.time=raw>>63?~raw+1:raw;
        w->environment.ticking=!(raw>>63);
        w->environment.received=net_millis();w->environment.known=1;return 0;
    }
    if(id==0x1f){
        unsigned reason=mc_u8(&r);float value=mc_f32(&r);
        if(r.error || r.pos!=n || !isfinite(value) || ((reason==7 || reason==8) && (value<0 || value>1)))return bad(w,"Invalid weather event");
        if(reason==1)w->environment.rain=1;
        if(reason==2)w->environment.rain=0;
        if(reason==7)w->environment.rain=value;
        if(reason==8)w->environment.thunder=value;
        return 0;
    }
    if(id==0x08 || id==0x09){
        uint64_t packed=mc_u64(&r);int x=sign_bits(packed>>38,26),y=sign_bits(packed&4095,12),z=sign_bits((packed>>12)&0x3ffffff,26);
        McBlockEntity incoming={0};
        if(id==0x08){incoming.type=mc_varint_read(&r);if(entity_nbt(&r,&incoming)<0)return bad(w,"Invalid block entity NBT");}
        else {incoming.action=mc_u8(&r);incoming.parameter=mc_u8(&r);incoming.type=mc_varint_read(&r);}
        if(r.error || r.pos!=n || incoming.type>INT32_MAX){free(incoming.nbt);return bad(w,"Invalid block entity update");}
        McChunk *c=&w->chunks[slot(floor16(x),floor16(z))];
        if(!c->blocks || c->x!=floor16(x) || c->z!=floor16(z) || y<w->min_y || y>=w->min_y+w->height){free(incoming.nbt);return 0;}
        incoming.pos=((y-w->min_y)<<8)|((z&15)<<4)|(x&15);
        unsigned i=0;while(i<c->entity_count && c->entities[i].pos!=incoming.pos)i++;
        if(id==0x09){
            /* Block Action carries a block registry ID, not a block-entity ID.
               New chests need an action record even when no NBT was sent. */
            const char *name=mc_dig_def(mc_world_get(w,x,y,z))->name;
            unsigned type;
            if(incoming.type==176 && !strcmp(name,"chest"))type=1;
            else if(incoming.type==410 && !strcmp(name,"trapped_chest"))type=2;
            else if(incoming.type==343 && !strcmp(name,"ender_chest"))type=3;
            else return 0;
            if(incoming.action!=1)return 0;
            incoming.type=type;
            if(i<c->entity_count){c->entities[i].action=incoming.action;c->entities[i].parameter=incoming.parameter;}
        }
        if(id==0x08 || i==c->entity_count){
            size_t old=i<c->entity_count?c->entities[i].size:0;
            size_t bytes=c->entity_bytes-old+incoming.size+(i==c->entity_count?sizeof(incoming):0);
            if(bytes>MC_BLOCK_ENTITY_BYTES || (i==c->entity_count && i>=MC_BLOCK_ENTITIES)){free(incoming.nbt);return bad(w,"Block entity cache limit exceeded");}
            if(i==c->entity_count){
                McBlockEntity *entries=realloc(c->entities,(i+1)*sizeof(*entries));
                if(!entries){free(incoming.nbt);return bad(w,"Block entity allocation failed");}
                c->entities=entries;c->entity_count++;
            }else {incoming.action=c->entities[i].action;incoming.parameter=c->entities[i].parameter;free(c->entities[i].nbt);}
            c->entities[i]=incoming;w->bytes=w->bytes-c->entity_bytes+bytes;c->entity_bytes=bytes;
        }
        w->dirty[slot(c->x,c->z)]|=1u<<((y-w->min_y)/16);w->revision++;w->block_entity_updates++;return 0;
    }
    if(id==0x22 || (id>=0x47 && id<=0x4b))return border_packet(w,id,&r);
    if(id==0x4e){
        int x=(int32_t)mc_varint_read(&r),z=(int32_t)mc_varint_read(&r);
        if(r.error || r.pos!=n)return bad(w,"Invalid chunk view center");
        w->center_x=x;w->center_z=z;w->center_updates++;return 0;
    }
    if(id==0x4f){
        unsigned distance=mc_varint_read(&r);
        if(r.error || r.pos!=n || distance>64)return bad(w,"Invalid server view distance");
        w->view_distance=distance;return 0;
    }
    if (id == 0x28) return join(w,&r);
    if (id == 0x41) {
        char name[128]; if (mc_string(&r,name,sizeof(name)) < 0) return bad(w,"Invalid respawn dimension");
        return select_dimension(w,name);
    }
    if (id == 0x24) return chunk(w,&r);
    if (id == 0x0d) return chunk_biomes(w,&r);
    if(id==0x27){
        int x=(int32_t)mc_varint_read(&r),z=(int32_t)mc_varint_read(&r);LightPatch patch;
        if(!w->ready || read_light(&r,w->height,&patch)<0)return bad(w,"Invalid light update");
        McChunk *c=&w->chunks[slot(x,z)];
        return c->blocks && c->x==x && c->z==z?apply_light(w,c,&patch,1):0;
    }
    if (id == 0x1e) {
        int x = (int32_t)mc_u32(&r), z = (int32_t)mc_u32(&r);
        if (r.error || r.pos != n) return bad(w,"Invalid chunk unload");
        w->unloads++;
        McChunk *c = &w->chunks[slot(x,z)];
        if (c->blocks && c->x == x && c->z == z) {
            w->bytes-=c->entity_bytes;free_entities(c);if(c->light){free(c->light);c->light=NULL;w->bytes-=(size_t)(w->height+32)*256;}memset(c->light_known,0,sizeof(c->light_known));free(c->blocks); c->blocks = NULL; w->loaded--; w->bytes -= (size_t)w->height*256*2; w->revision++;
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
