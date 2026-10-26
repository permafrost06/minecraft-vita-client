#include "mesh.h"
#include "player.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MC_BLOCKINFO_BLOCKS
#include "blockinfo.inc"
#undef MC_BLOCKINFO_BLOCKS

#define BLOCK_DEFS (sizeof(block_defs)/sizeof(block_defs[0]))
#define STATES 32768
#define P MC_MESH_PAD
#define AT(x,y,z) ((((y)+1)*P + (z)+1)*P + (x)+1)
static uint16_t state_block[STATES];
static uint16_t unknown_block;
void mc_mesh_init(void) {
    unknown_block = 1;
    for (unsigned i = 0; i < STATES; i++) state_block[i] = unknown_block;
    for (unsigned i = 0; i < BLOCK_DEFS; i++)
        for (unsigned s = block_defs[i].low; s <= block_defs[i].high && s < STATES; s++) state_block[s] = i;
}
const McBlockDef *mc_block_def(uint16_t state) { return &block_defs[state < STATES ? state_block[state] : unknown_block]; }

int mc_mesh_alloc(McMesh *m) {
    memset(m, 0, sizeof(*m));
    for (int p = 0; p < MC_PASSES; p++)
        if (!(m->pass[p] = malloc(sizeof(McVertex)*4*MC_MESH_MAX_QUADS))) { mc_mesh_free(m); return -1; }
    return 0;
}
void mc_mesh_free(McMesh *m) { for (int p = 0; p < MC_PASSES; p++) { free(m->pass[p]); m->pass[p] = NULL; } }

static int blocks_light(const McBlockDef *d) { return (d->flags & (MF_OPAQUE|MF_WATER)) != 0 || d->model == MM_LIQUID; }
void mc_mesh_heightmap(const McWorld *w, const McChunk *c, McHeightmap out) {
    for (int i = 0; i < 256; i++) {
        int y = w->height-1;
        while (y >= 0 && !blocks_light(mc_block_def(c->blocks[y*256+i]))) y--;
        out[i] = (int16_t)(w->min_y + y + 1);
    }
}

/* Per-section scratch: padded block states plus derived per-cell properties. */
static const uint16_t *grid;
static const int16_t *sky;
static const uint16_t *server_light;
static uint8_t solid[P*P*P];
static const McBlockDef *defs[P*P*P];
static McMesh *mesh;
static int base_y;
static const int16_t (*tints)[64];
static int16_t tint_now;static int tint_top_only,tint_cell;

/* Directions: -x, +x, -y, +y, -z, +z. R and U span the face as seen from outside
   with R x U = normal, so (U, origin, R, R+U) is counter-clockwise. */
static const int8_t N[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
static const int8_t R[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{1,0,0},{-1,0,0},{1,0,0}};
static const int8_t U[6][3] = {{0,1,0},{0,1,0},{0,0,1},{0,0,-1},{0,1,0},{0,1,0}};
static const float face_shade[6] = {0.7f, 0.7f, 0.55f, 1.0f, 0.85f, 0.85f};
static const float ao_curve[4] = {1.0f, 0.78f, 0.62f, 0.48f};

static float sampled_sky, sampled_block;
static float sky_light(int x, int y, int z) {
    unsigned light=server_light[AT(x,y,z)];
    if(light!=256){sampled_sky=(light&15)/15.0f;sampled_block=(light>>4)/15.0f;}
    else {
        int h=sky[(z+1)*P+x+1],depth=h-(base_y+y);
        float v=depth<=0?1:fmaxf(.32f,1-depth*.065f);
        /* Invert the brightness curve so fallback light uses the same shader. */
        float t=(v-.1f)/.9f;sampled_sky=4*t/(1+3*t);sampled_block=0;
    }
    float level=fmaxf(sampled_sky,sampled_block);return .1f+.9f*level/(4-3*level);
}
static float lighting(const McBlockDef *d,int x,int y,int z){
    if(d->flags&MF_EMISSIVE){sampled_sky=0;sampled_block=1;return 1;}
    return sky_light(x,y,z);
}

static uint16_t texture_for(const McBlockDef *d, uint16_t state, int dir) {
    int axis = 1;
    if (d->flags & MF_AXIS) { axis = (state - d->low) % 3; }
    int face_axis = dir/2;
    if (face_axis == axis) return dir == 2 && !(d->flags & MF_AXIS) ? d->bottom : d->top;
    return d->side;
}
static int coord(int value, int sign_positive) { return sign_positive ? value : 16 - value; }
/* Emits one quad. lo/hi are box bounds in 1/16 units; light holds per-corner
   brightness in order (U, origin, R, R+U). */
static void quad(int pass, int bx, int by, int bz, const int lo[3], const int hi[3], int dir, uint16_t tile, const float light[4], int flip) {
    if (mesh->total >= MC_MESH_MAX_QUADS) { mesh->dropped++; return; }
    McVertex *v = mesh->pass[pass] + mesh->quads[pass]*4;
    int n = dir/2, r = 0, u = 0;
    for (int a = 0; a < 3; a++) { if (R[dir][a]) r = a; if (U[dir][a]) u = a; }
    int tx = (tile % MC_ATLAS_TILES)*256, ty = (tile / MC_ATLAS_TILES)*256;
    static const int corner[4][2] = {{0,1},{0,0},{1,0},{1,1}};
    for (int i = 0; i < 4; i++) {
        int k = (i + flip) & 3, cr = corner[k][0], cu = corner[k][1], p[3];
        p[n] = N[dir][n] > 0 ? hi[n] : lo[n];
        p[r] = (R[dir][r] > 0) == (cr == 1) ? hi[r] : lo[r];
        p[u] = (U[dir][u] > 0) == (cu == 1) ? hi[u] : lo[u];
        int tu = coord(p[r], R[dir][r] > 0), tv = 16 - coord(p[u], U[dir][u] > 0);
        tu = tu*16; tv = tv*16;
        if (tu < 1) tu = 1; else if (tu > 255) tu = 255;
        if (tv < 1) tv = 1; else if (tv > 255) tv = 255;
        uint8_t l = (uint8_t)(light[k]*255 > 255 ? 255 : light[k]*255);
        v[i] = (McVertex){(int16_t)(bx*16+p[0]), (int16_t)(by*16+p[1]), (int16_t)(bz*16+p[2]), tint_top_only && dir != 3 ? 0 : tint_now,
                          (int16_t)(tx+tu), (int16_t)(ty+tv), l, (uint8_t)(sampled_sky*255), (uint8_t)(sampled_block*255), 255};
    }
    mesh->quads[pass]++; mesh->total++;
}
static int hides(const McBlockDef *self, const McBlockDef *other, uint16_t self_state, uint16_t other_state) {
    if (other->flags & MF_OPAQUE) return 1;
    if (self->flags & MF_SAMECULL && other == self) return 1;
    (void)self_state; (void)other_state;
    return 0;
}
static void cube(int x, int y, int z, const McBlockDef *d, uint16_t state) {
    int lo[3] = {0,0,0}, hi[3] = {16,16,16};
    for (int dir = 0; dir < 6; dir++) {
        int fx = x+N[dir][0], fy = y+N[dir][1], fz = z+N[dir][2];
        if (hides(d, defs[AT(fx,fy,fz)], state, grid[AT(fx,fy,fz)])) continue;
        float base = lighting(d,fx,fy,fz)*((d->flags&MF_EMISSIVE)?1:face_shade[dir]), light[4]; int ao[4];
        static const int corner[4][2] = {{0,1},{0,0},{1,0},{1,1}};
        for (int i = 0; i < 4; i++) {
            int sr = corner[i][0] ? 1 : -1, su = corner[i][1] ? 1 : -1;
            int ax = fx+R[dir][0]*sr, ay = fy+R[dir][1]*sr, az = fz+R[dir][2]*sr;
            int bx = fx+U[dir][0]*su, by = fy+U[dir][1]*su, bz = fz+U[dir][2]*su;
            int s1 = solid[AT(ax,ay,az)], s2 = solid[AT(bx,by,bz)];
            int c = solid[AT(ax+U[dir][0]*su, ay+U[dir][1]*su, az+U[dir][2]*su)];
            ao[i] = s1 && s2 ? 3 : s1+s2+c;
            light[i] = (d->flags & MF_EMISSIVE) ? 1.0f : base*ao_curve[ao[i]];
        }
        quad(d->pass, x, y, z, lo, hi, dir, texture_for(d, state, dir), light, ao[0]+ao[2] > ao[1]+ao[3]);
    }
}
static void box(int x, int y, int z, const McBlockDef *d, uint16_t state, const int lo[3], const int hi[3], int cull) {
    for (int dir = 0; dir < 6; dir++) {
        int n = dir/2, edge = N[dir][n] > 0 ? hi[n] >= 16 : lo[n] <= 0;
        int fx = x, fy = y, fz = z;
        if (edge) {
            fx += N[dir][0]; fy += N[dir][1]; fz += N[dir][2];
            if (cull && hides(d, defs[AT(fx,fy,fz)], state, grid[AT(fx,fy,fz)])) continue;
        }
        float l = lighting(d,fx,fy,fz)*((d->flags&MF_EMISSIVE)?1:face_shade[dir]), light[4] = {l,l,l,l};
        quad(d->pass, x, y, z, lo, hi, dir, texture_for(d, state, dir), light, 0);
    }
}
static int is_liquid(const McBlockDef *d, const McBlockDef *liquid) {
    if (liquid->flags & MF_WATER) return (d->flags & MF_WATER) != 0;
    return d == liquid;
}
static void liquid(int x, int y, int z, const McBlockDef *d, int pass) {
    int16_t saved = tint_now; int saved_top = tint_top_only;
    tint_now = (d->flags & MF_WATER) ? tints[2][tint_cell] : 0; tint_top_only = 0;
    const McBlockDef *above = defs[AT(x,y+1,z)];
    int top = is_liquid(above, d) ? 16 : 14;
    int lo[3] = {0,0,0}, hi[3] = {16,top,16};
    for (int dir = 0; dir < 6; dir++) {
        int fx = x+N[dir][0], fy = y+N[dir][1], fz = z+N[dir][2];
        const McBlockDef *o = defs[AT(fx,fy,fz)];
        if (is_liquid(o, d)) continue;
        if (o->flags & MF_OPAQUE && !(dir == 3 && top < 16)) continue;
        if (dir == 3 && top == 16) continue;
        float l = lighting(d,fx,fy,fz)*((d->flags&MF_EMISSIVE)?1:face_shade[dir]), light[4] = {l,l,l,l};
        quad(pass, x, y, z, lo, hi, dir, d->side, light, 0);
    }
    tint_now = saved; tint_top_only = saved_top;
}
static void cross(int x, int y, int z, const McBlockDef *d) {
    float l = lighting(d,x,y,z)*((d->flags&MF_EMISSIVE)?1:.92f);
    if (mesh->total+2 > MC_MESH_MAX_QUADS) { mesh->dropped += 2; return; }
    int tx = (d->side % MC_ATLAS_TILES)*256, ty = (d->side / MC_ATLAS_TILES)*256;
    uint8_t c = (uint8_t)(l*255);
    static const int16_t diag[2][4][2] = {{{2,2},{2,2},{14,14},{14,14}}, {{14,2},{14,2},{2,14},{2,14}}};
    static const int16_t uv[4][2] = {{1,1},{1,255},{255,255},{255,1}};
    for (int k = 0; k < 2; k++) {
        McVertex *v = mesh->pass[MC_PASS_CUTOUT] + mesh->quads[MC_PASS_CUTOUT]*4;
        for (int i = 0; i < 4; i++) {
            int high = i == 0 || i == 3;
            v[i] = (McVertex){(int16_t)(x*16+diag[k][i][0]), (int16_t)(y*16+(high ? 16 : 0)), (int16_t)(z*16+diag[k][i][1]), tint_now,
                              (int16_t)(tx+uv[i][0]), (int16_t)(ty+uv[i][1]), c, (uint8_t)(sampled_sky*255), (uint8_t)(sampled_block*255), 255};
        }
        mesh->quads[MC_PASS_CUTOUT]++; mesh->total++;
    }
}
/* Dedicated presentation boxes use local south-facing coordinates, rotated by state. */
static void model_box(int x,int y,int z,const McBlockDef *d,uint16_t state,int a,int b,int c,int e,int f,int g,float angle){
    unsigned first=mesh->quads[d->pass];int lo[3]={a,b,c},hi[3]={e,f,g};
    box(x,y,z,d,state,lo,hi,0);
    float co=cosf(angle),si=sinf(angle);
    for(unsigned q=first;q<mesh->quads[d->pass];q++)for(int k=0;k<4;k++){
        McVertex *v=mesh->pass[d->pass]+q*4+k;float vx=v->x-x*16-8,vz=v->z-z*16-8;
        v->x=x*16+8+lroundf(vx*co-vz*si);v->z=z*16+8+lroundf(vx*si+vz*co);
    }
}
static void presentation(int x,int y,int z,const McBlockDef *d,uint16_t s,int open){
    unsigned direction=d->orient_stride?(s-d->low)/d->orient_stride%d->orient_count:0;
    static const float cardinal[4]={3.14159265f,0,1.57079633f,-1.57079633f};
    float angle=d->orient_count==16?direction*6.28318531f/16:cardinal[direction%4];
    int wall=d->model==MM_WALL_SIGN || d->model==MM_WALL_BANNER;
    if(d->model==MM_CHEST){
        model_box(x,y,z,d,s,1,0,1,15,10,15,angle);
        if(open)model_box(x,y,z,d,s,1,10,1,15,24,5,angle);
        else model_box(x,y,z,d,s,1,10,1,15,14,15,angle);
        model_box(x,y,z,d,s,7,8,14,9,12,16,angle);return;
    }
    if(!wall)model_box(x,y,z,d,s,7,0,7,9,16,9,angle);
    if(d->model==MM_SIGN || d->model==MM_WALL_SIGN)
        model_box(x,y,z,d,s,0,wall?4:8,wall?0:7,16,wall?12:16,wall?2:9,angle);
    else model_box(x,y,z,d,s,1,wall?0:3,wall?0:7,15,wall?16:19,wall?2:9,angle);
}
/* Ratio of a biome colour to the plains colour the textures were authored for. */
static int16_t pack_tint(uint32_t color, uint32_t base) {
    unsigned packed = 0, neutral = 1;
    for (int shift = 16; shift >= 0; shift -= 8) {
        float ratio = (float)((color >> shift) & 255) / (float)(((base >> shift) & 255) ? ((base >> shift) & 255) : 1);
        long q = lroundf(ratio*15.5f); if (q < 0) q = 0; if (q > 31) q = 31;
        if (q != 16 && q != 15) neutral = 0;
        packed = (packed << 5) | (unsigned)q;
    }
    return neutral ? 0 : (int16_t)(-1 - (int)packed);
}
void mc_mesh_gather(const McWorld *w, int cx, int sy, int cz, const int16_t *const heights[9], McMeshInput *in) {
    memset(in->open,0,sizeof(in->open));
    const McChunk *center=mc_world_chunk(w,cx,cz);
    if(center)for(unsigned i=0;i<center->entity_count;i++){const McBlockEntity *e=center->entities+i;
        if(e->pos/4096==(unsigned)sy && e->action==1)in->open[e->pos%4096]=e->parameter!=0;
    }
    in->base_y = w->min_y + sy*16; in->empty = 1;
    int plains = w->plains_biome;
    uint32_t base[3] = {plains >= 0 ? w->biome_grass[plains] : mc_biome_grass_color(.8f,.4f),
                        plains >= 0 ? w->biome_foliage[plains] : mc_biome_foliage_color(.8f,.4f), plains >= 0 ? w->biome_water[plains] : 0x3f76e4};
    for (int cell = 0; cell < 64; cell++) {
        int biome = w->biome_count ? mc_world_biome(w, cx*16 + (cell%4)*4, in->base_y + (cell/16)*4, cz*16 + ((cell/4)%4)*4) : -1;
        for (int k = 0; k < 3; k++) {
            uint32_t color = biome < 0 || biome >= w->biome_count ? base[k] : k == 0 ? w->biome_grass[biome] : k == 1 ? w->biome_foliage[biome] : w->biome_water[biome];
            in->tint[k][cell] = pack_tint(color, base[k]);
        }
    }
    const McChunk *chunks[9];
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) chunks[(dz+1)*3+dx+1] = mc_world_chunk(w, cx+dx, cz+dz);
    for (int z = -1; z <= 16; z++) for (int x = -1; x <= 16; x++) {
        int nx = x < 0 ? 0 : x > 15 ? 2 : 1, nz = z < 0 ? 0 : z > 15 ? 2 : 1;
        const int16_t *h = heights[nz*3+nx];
        in->sky[(z+1)*P + x+1] = h ? h[((unsigned)z & 15)*16 + ((unsigned)x & 15)] : (int16_t)-32768;
    }
    for (int y = -1; y <= 16; y++) {
        int wy = in->base_y + y, inside = wy >= w->min_y && wy < w->min_y + w->height;
        for (int z = -1; z <= 16; z++) {
            int nz = z < 0 ? 0 : z > 15 ? 2 : 1;
            uint16_t *row = in->grid + AT(-1,y,z);
            for (int x = -1; x <= 16; x++) {
                const McChunk *c = chunks[nz*3 + (x < 0 ? 0 : x > 15 ? 2 : 1)];
                /* Missing neighbors are treated as solid so edges do not show walls. */
                uint16_t s = !c ? 1 : inside ? c->blocks[(wy - w->min_y)*256 + ((unsigned)z & 15)*16 + ((unsigned)x & 15)] : 0;
                row[x+1] = s;
                int light=mc_world_light(w,cx*16+x,wy,cz*16+z);
                in->light[AT(x,y,z)]=light<0?256:light;
                if (s && (unsigned)x < 16 && (unsigned)y < 16 && (unsigned)z < 16) in->empty = 0;
            }
        }
    }
}
void mc_mesh_build(const McMeshInput *in, McMesh *out) {
    mesh = out; out->quads[0] = out->quads[1] = out->quads[2] = 0; out->total = out->dropped = 0;
    if (in->empty) return;
    grid = in->grid; sky = in->sky;server_light=in->light; base_y = in->base_y; tints = in->tint;
    for (int i = 0; i < P*P*P; i++) { defs[i] = mc_block_def(grid[i]); solid[i] = defs[i]->flags & MF_OPAQUE; }
    for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        int i = AT(x,y,z); uint16_t s = grid[i]; const McBlockDef *d = defs[i];
        if (!s) continue;
        tint_cell = (y/4)*16 + (z/4)*4 + x/4;
        tint_now = (d->flags & MF_TINT_GRASS) ? tints[0][tint_cell] : (d->flags & MF_TINT_FOLIAGE) ? tints[1][tint_cell] : 0;
        tint_top_only = d->model == MM_CUBE && (d->flags & MF_TINT_GRASS);
        int waterlogged = d->water_stride && ((s - d->low) / d->water_stride) % 2 == 0;
        if (d->model == MM_NONE) { if ((d->flags & MF_WATER) || waterlogged) liquid(x, y, z, mc_block_def(80), MC_PASS_TRANSLUCENT); continue; }
        if (waterlogged && d->model != MM_LIQUID) liquid(x, y, z, mc_block_def(80), MC_PASS_TRANSLUCENT);
        switch (d->model) {
        case MM_SIGN: case MM_WALL_SIGN: case MM_BANNER: case MM_WALL_BANNER: case MM_CHEST:
            presentation(x,y,z,d,s,in->open[y*256+z*16+x]);break;
        case MM_CUBE: cube(x, y, z, d, s); break;
        case MM_LIQUID: liquid(x, y, z, d, d->pass); break;
        case MM_CROSS:
            cross(x, y, z, d);
            if (d->flags & MF_WATER) liquid(x, y, z, mc_block_def(80), MC_PASS_TRANSLUCENT);
            break;
        case MM_FLAT: { int lo[3] = {0,0,0}, hi[3] = {16,1,16}; box(x, y, z, d, s, lo, hi, 0); break; }
        case MM_PLATE: { int lo[3] = {1,0,1}, hi[3] = {15,1,15}; box(x, y, z, d, s, lo, hi, 1); break; }
        case MM_SNOW: { int lo[3] = {0,0,0}, hi[3] = {16,2*((s - d->low) % 8 + 1),16}; box(x, y, z, d, s, lo, hi, 1); break; }
        case MM_TORCH: { int lo[3] = {7,0,7}, hi[3] = {9,10,9}; box(x, y, z, d, s, lo, hi, 0); break; }
        case MM_BOXES: {
            unsigned count; const McBlockBox *b = mc_block_render_boxes(s, &count);
            for (unsigned k = 0; k < count; k++) {
                int lo[3], hi[3];
                for (int a = 0; a < 3; a++) {
                    lo[a] = b[k].bounds[a]/2; hi[a] = (b[k].bounds[a+3]+1)/2;
                    if (lo[a] < 0) lo[a] = 0;
                    if (hi[a] > 16) hi[a] = 16;
                }
                if (hi[0] > lo[0] && hi[1] > lo[1] && hi[2] > lo[2]) box(x, y, z, d, s, lo, hi, 1);
            }
            if (d->flags & MF_WATER) liquid(x, y, z, mc_block_def(80), MC_PASS_TRANSLUCENT);
            break;
        }
        }
    }
}
void mc_mesh_section(const McWorld *w, int cx, int sy, int cz, const int16_t *const heights[9], McMesh *out) {
    static McMeshInput in;
    mc_mesh_gather(w, cx, sy, cz, heights, &in);
    mc_mesh_build(&in, out);
}
