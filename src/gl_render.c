#include "gl_render.h"
#include "mesh.h"
#include "blocks.h"
#include <vitaGL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include "debugScreen.h"
#include "debugScreenFont.c"

#define PI 3.14159265358979323846f
#define SECTIONS 32
#define FOG_START 38.0f
#define FOG_END 62.0f
#define SKY_TOP 0x5d8fe0
#define SKY_HORIZON 0xb4d0f0

typedef struct { GLuint vbo; uint16_t quads[MC_PASSES]; uint8_t dirty; } Section;
typedef struct {
    int present, x, z, has_height; uint64_t serial; uint16_t neighbors;
    McHeightmap height; Section s[SECTIONS];
} Column;
static Column columns[MC_CACHE_COUNT];
static McGlStats stats;
static GLuint terrain_program, cutout_program, overlay_program, atlas, font, quad_indices;
typedef struct { GLint rows[4], offset, fog, fog_color, tex; } TerrainUniforms;
static TerrainUniforms uniforms[2];
static GLint overlay_screen, overlay_tex;
static int world_min_y, sections_per_column;

static const char *terrain_vs =
    "void main(float3 position, float2 texcoord, float4 color,\n"
    "  uniform float4 row0, uniform float4 row1, uniform float4 row2, uniform float4 row3,\n"
    "  uniform float3 offset, uniform float2 fog,\n"
    "  out float4 vPosition : POSITION, out float2 vTexcoord : TEXCOORD0,\n"
    "  out float4 vColor : COLOR, out float vFog : TEXCOORD1)\n"
    "{\n"
    "  float4 p = float4(position * 0.0625 + offset, 1.0);\n"
    "  vPosition = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));\n"
    "  vTexcoord = texcoord * (1.0 / 8192.0);\n"
    "  vColor = color;\n"
    "  vFog = saturate((length(p.xz) - fog.x) * fog.y);\n"
    "}\n";
static const char *terrain_fs =
    "float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR, float vFog : TEXCOORD1,\n"
    "  uniform sampler2D tex, uniform float4 fogColor) : COLOR\n"
    "{\n"
    "  float4 c = tex2D(tex, vTexcoord) * vColor;\n"
    "  c.rgb = lerp(c.rgb, fogColor.rgb, vFog);\n"
    "  return c;\n"
    "}\n";
static const char *cutout_fs =
    "float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR, float vFog : TEXCOORD1,\n"
    "  uniform sampler2D tex, uniform float4 fogColor) : COLOR\n"
    "{\n"
    "  float4 c = tex2D(tex, vTexcoord) * vColor;\n"
    "  if (c.a < 0.5) discard;\n"
    "  c.rgb = lerp(c.rgb, fogColor.rgb, vFog);\n"
    "  return c;\n"
    "}\n";
static const char *overlay_vs =
    "void main(float2 position, float2 texcoord, float4 color, uniform float2 screen,\n"
    "  out float4 vPosition : POSITION, out float2 vTexcoord : TEXCOORD0, out float4 vColor : COLOR)\n"
    "{\n"
    "  vPosition = float4(position.x * screen.x - 1.0, 1.0 - position.y * screen.y, 0.0, 1.0);\n"
    "  vTexcoord = texcoord;\n"
    "  vColor = color;\n"
    "}\n";
static const char *overlay_fs =
    "float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR, uniform sampler2D tex) : COLOR\n"
    "{\n"
    "  return tex2D(tex, vTexcoord) * vColor;\n"
    "}\n";

static void stage(const char *text) {
    FILE *f = fopen("ux0:data/vitablock/runtime.log", "a");
    if (f) { fprintf(f, "gl: %s\n", text); fclose(f); }
}
static GLint attribs[3][3];
static GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0}; glGetShaderInfoLog(s, sizeof(log), NULL, log);
        FILE *f = fopen("ux0:data/vitablock/shader.log", "a");
        if (f) { fprintf(f, "%s\n%s\n", log, source); fclose(f); }
    }
    return s;
}
static GLuint program(const char *vs, const char *fs, int index) {
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_CG_VERTEX_SHADER_EXT, vs));
    glAttachShader(p, shader(GL_CG_FRAGMENT_SHADER_EXT, fs));
    glLinkProgram(p);
    static const char *names[3] = {"position", "texcoord", "color"};
    for (int i = 0; i < 3; i++) attribs[index][i] = glGetAttribLocation(p, names[i]);
    if (attribs[index][0] < 0 || attribs[index][1] < 0 || attribs[index][2] < 0) stage("missing shader attribute");
    return p;
}
static void terrain_uniforms(TerrainUniforms *u, GLuint p) {
    static const char *names[4] = {"row0", "row1", "row2", "row3"};
    for (int i = 0; i < 4; i++) u->rows[i] = glGetUniformLocation(p, names[i]);
    u->offset = glGetUniformLocation(p, "offset");
    u->fog = glGetUniformLocation(p, "fog");
    u->fog_color = glGetUniformLocation(p, "fogColor");
    u->tex = glGetUniformLocation(p, "tex");
}

/* Overlay batching: 2D quads accumulated per frame and drawn in one call. */
typedef struct { float x, y, u, v; uint32_t color; } OverlayVertex;
#define OVERLAY_MAX 6000
static OverlayVertex overlay[OVERLAY_MAX*6];
static unsigned overlay_count;
static uint32_t abgr(uint32_t argb) {
    return (argb & 0xff00ff00) | ((argb >> 16) & 255) | ((argb & 255) << 16);
}
static void overlay_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color) {
    if (overlay_count >= OVERLAY_MAX) return;
    OverlayVertex *v = overlay + overlay_count*6; uint32_t c = abgr(color);
    OverlayVertex a = {x, y, u0, v0, c}, b = {x, y+h, u0, v1, c}, d = {x+w, y+h, u1, v1, c}, e = {x+w, y, u1, v0, c};
    v[0] = a; v[1] = b; v[2] = d; v[3] = a; v[4] = d; v[5] = e;
    overlay_count++;
}
void mc_gl_rect(float x, float y, float w, float h, uint32_t color) {
    overlay_quad(x, y, w, h, 0.5f/128, 0.5f/128, 7.5f/128, 7.5f/128, color);
}
float mc_gl_text_width(const char *text, float scale) { return strlen(text)*8*scale; }
void mc_gl_text(float x, float y, float scale, uint32_t color, const char *text) {
    float start = x;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '\n') { x = start; y += 10*scale; continue; }
        if (*c != ' ' && *c) {
            float u = (*c % 16)*8/128.0f, v = (*c / 16)*8/128.0f;
            overlay_quad(x+scale, y+scale, 8*scale, 8*scale, u, v, u+8/128.0f, v+8/128.0f, color & 0xbf000000);
            overlay_quad(x, y, 8*scale, 8*scale, u, v, u+8/128.0f, v+8/128.0f, color);
        }
        x += 8*scale;
    }
}
void mc_gl_textf(float x, float y, float scale, uint32_t color, const char *format, ...) {
    char text[512]; va_list args; va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    mc_gl_text(x, y, scale, color, text);
}

static void build_font(void) {
    static uint32_t pixels[128*128];
    for (int g = 1; g < 256; g++) {
        const unsigned char *rows = psvDebugScreenFont.glyphs + g*8;
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
            pixels[((g/16)*8+y)*128 + (g%16)*8+x] = (rows[y] & (0x80 >> x)) ? 0xffffffff : 0x00ffffff;
    }
    /* Glyph 0 is a solid block for rectangles. */
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) pixels[y*128+x] = 0xffffffff;
    glGenTextures(1, &font); glBindTexture(GL_TEXTURE_2D, font);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
static void build_atlas(void) {
    uint32_t *pixels = malloc(sizeof(uint32_t)*MC_ATLAS_SIZE*MC_ATLAS_SIZE);
    if (!pixels) return;
    mc_textures_build(pixels);
    glGenTextures(1, &atlas); glBindTexture(GL_TEXTURE_2D, atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MC_ATLAS_SIZE, MC_ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    /* Two CPU box-filtered levels (8px and 4px tiles). The GPU generator mixes
       neighboring atlas tiles, and smaller levels would merge tiles anyway. */
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, MC_ATLAS_SIZE/4, MC_ATLAS_SIZE/4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    for (int level = 1, size = MC_ATLAS_SIZE/2; level <= 2; level++, size /= 2) {
        for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
            unsigned sum[4] = {0};
            for (int k = 0; k < 4; k++) {
                uint32_t p = pixels[(y*2 + k/2)*size*2 + x*2 + k%2];
                for (int c = 0; c < 4; c++) sum[c] += (p >> (c*8)) & 255;
            }
            pixels[y*size + x] = (sum[0]/4) | (sum[1]/4) << 8 | (sum[2]/4) << 16 | (sum[3]/4) << 24;
        }
        glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(pixels);
}

int mc_gl_init(void) {
    /* Full-precision varyings: atlas texcoords need more than fp16 to avoid tile bleeding. */
    vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_DISABLE, SHARK_DISABLE);
    /* This vitaGL build's init return value is not a reliable success flag; probe the context instead. */
    vglInitExtended(0x100000, 960, 544, 16*1024*1024, SCE_GXM_MULTISAMPLE_4X);
    {
        const GLubyte *renderer = glGetString(GL_RENDERER);
        FILE *f = fopen("ux0:data/vitablock/runtime.log", "a");
        if (f) { fprintf(f, "GL renderer: %s version: %s\n", renderer ? (const char *)renderer : "(null)", glGetString(GL_VERSION) ? (const char *)glGetString(GL_VERSION) : "(null)"); fclose(f); }
        if (!renderer) return -1;
    }
    mc_mesh_init();
    terrain_program = program(terrain_vs, terrain_fs, 0);
    cutout_program = program(terrain_vs, cutout_fs, 1);
    overlay_program = program(overlay_vs, overlay_fs, 2);
    terrain_uniforms(&uniforms[0], terrain_program);
    terrain_uniforms(&uniforms[1], cutout_program);
    overlay_screen = glGetUniformLocation(overlay_program, "screen");
    overlay_tex = glGetUniformLocation(overlay_program, "tex");
    build_atlas(); build_font();
    /* One shared index buffer: quad q uses vertices 4q..4q+3. */
    uint16_t *indices = malloc(sizeof(uint16_t)*6*MC_MESH_MAX_QUADS);
    if (!indices) return -3;
    for (unsigned q = 0; q < MC_MESH_MAX_QUADS; q++) {
        uint16_t b = (uint16_t)(q*4);
        uint16_t *i = indices + q*6; i[0] = b; i[1] = b+1; i[2] = b+2; i[3] = b; i[4] = b+2; i[5] = b+3;
    }
    glGenBuffers(1, &quad_indices); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t)*6*MC_MESH_MAX_QUADS, indices, GL_STATIC_DRAW);
    free(indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glClearColor(((SKY_HORIZON >> 16) & 255)/255.0f, ((SKY_HORIZON >> 8) & 255)/255.0f, (SKY_HORIZON & 255)/255.0f, 1);
    return 0;
}

static void free_column(Column *c) {
    for (int i = 0; i < SECTIONS; i++) if (c->s[i].vbo) glDeleteBuffers(1, &c->s[i].vbo);
    memset(c, 0, sizeof(*c));
}
void mc_gl_forget(void) { for (int i = 0; i < MC_CACHE_COUNT; i++) free_column(&columns[i]); }

/* Meshing runs on a worker thread on another core. Column bookkeeping and the
   result slots are protected by the world mutex; the worker holds it only while
   choosing a section and copying its neighborhood, never while building. */
#define RESULTS 3
enum { RESULT_FREE, RESULT_BUILDING, RESULT_READY };
typedef struct { McMesh mesh; int state, slot, sy; uint64_t serial; } Result;
static Result results[RESULTS];
static McMeshInput worker_input;
static McWorld *worker_world;
static SceUID worker_mutex = -1, worker_thread = -1;
static volatile int worker_running;
static float worker_camera[3];
static uint64_t built_sections, build_us;

static void mark_all(Column *c) { for (int i = 0; i < SECTIONS; i++) c->s[i].dirty = 1; }
static Column *column_at(const McWorld *w, int x, int z) {
    const McChunk *chunk = mc_world_chunk(w, x, z);
    if (!chunk) return NULL;
    Column *c = &columns[chunk - w->chunks];
    return c->present && c->x == x && c->z == z ? c : NULL;
}
/* Nearest dirty section, preferring columns whose neighbors have all arrived. */
static Column *next_dirty(int *section, unsigned *pending_out) {
    Column *best = NULL; float best_d = 1e30f; unsigned pending = 0;
    for (int i = 0; i < MC_CACHE_COUNT; i++) {
        Column *c = &columns[i];
        if (!c->present) continue;
        float dx = c->x*16+8 - worker_camera[0], dz = c->z*16+8 - worker_camera[2];
        float penalty = c->neighbors == 0xff ? 0 : 100000.0f;
        for (int s = 0; s < sections_per_column; s++) {
            if (!c->s[s].dirty) continue;
            pending++;
            float dy = world_min_y + s*16+8 - worker_camera[1], d = dx*dx+dy*dy+dz*dz + penalty;
            if (d < best_d) { best_d = d; best = c; *section = s; }
        }
    }
    if (pending_out) *pending_out = pending;
    return best;
}
static int mesh_worker(SceSize args, void *argp) {
    (void)args; (void)argp;
    while (worker_running) {
        sceKernelLockMutex(worker_mutex, 1, NULL);
        Result *r = NULL; int sy = 0; Column *c = NULL;
        for (int i = 0; i < RESULTS && !r; i++) if (results[i].state == RESULT_FREE) r = &results[i];
        if (r) c = next_dirty(&sy, NULL);
        if (!r || !c) { sceKernelUnlockMutex(worker_mutex, 1); sceKernelDelayThread(2000); continue; }
        const int16_t *heights[9];
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            Column *n = column_at(worker_world, c->x+dx, c->z+dz);
            heights[(dz+1)*3+dx+1] = n && n->has_height ? n->height : NULL;
        }
        mc_mesh_gather(worker_world, c->x, sy, c->z, heights, &worker_input);
        c->s[sy].dirty = 0;
        r->state = RESULT_BUILDING; r->slot = c - columns; r->serial = c->serial; r->sy = sy;
        sceKernelUnlockMutex(worker_mutex, 1);
        uint64_t start = sceKernelGetProcessTimeWide();
        mc_mesh_build(&worker_input, &r->mesh);
        uint64_t spent = sceKernelGetProcessTimeWide() - start;
        sceKernelLockMutex(worker_mutex, 1, NULL);
        r->state = RESULT_READY; built_sections++; build_us += spent;
        sceKernelUnlockMutex(worker_mutex, 1);
    }
    return 0;
}
int mc_gl_start_worker(McWorld *world, SceUID mutex) {
    for (int i = 0; i < RESULTS; i++) if (mc_mesh_alloc(&results[i].mesh) < 0) return -1;
    worker_world = world; worker_mutex = mutex; worker_running = 1;
    worker_thread = sceKernelCreateThread("mcmesher", mesh_worker, 0x10000110, 64*1024, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
    if (worker_thread < 0) return worker_thread;
    return sceKernelStartThread(worker_thread, 0, NULL);
}
void mc_gl_term(void) {
    if (worker_thread >= 0) {
        worker_running = 0;
        sceKernelWaitThreadEnd(worker_thread, NULL, NULL); sceKernelDeleteThread(worker_thread); worker_thread = -1;
    }
    mc_gl_forget();
    for (int i = 0; i < RESULTS; i++) mc_mesh_free(&results[i].mesh);
}
static void upload(Section *s, const McMesh *m) {
    stats.dropped += m->dropped;
    for (int p = 0; p < MC_PASSES; p++) s->quads[p] = m->quads[p];
    if (!m->total) { if (s->vbo) { glDeleteBuffers(1, &s->vbo); s->vbo = 0; } return; }
    if (!s->vbo) glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    size_t offset = 0;
    glBufferData(GL_ARRAY_BUFFER, sizeof(McVertex)*4*m->total, NULL, GL_STATIC_DRAW);
    for (int p = 0; p < MC_PASSES; p++) {
        size_t n = sizeof(McVertex)*4*m->quads[p];
        if (n) glBufferSubData(GL_ARRAY_BUFFER, offset, n, m->pass[p]);
        offset += n;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void mc_gl_update(McWorld *w, const McCamera *camera, unsigned budget_us) {
    (void)budget_us;
    uint64_t start = sceKernelGetProcessTimeWide();
    world_min_y = w->min_y; sections_per_column = w->height/16;
    if (sections_per_column > SECTIONS) sections_per_column = SECTIONS;
    worker_camera[0] = (float)camera->x; worker_camera[1] = (float)camera->y; worker_camera[2] = (float)camera->z;
    /* Reconcile renderer columns with the world cache. */
    for (int i = 0; i < MC_CACHE_COUNT; i++) {
        const McChunk *chunk = &w->chunks[i]; Column *c = &columns[i];
        if (!chunk->blocks) { if (c->present) free_column(c); w->dirty[i] = 0; continue; }
        if (!c->present || c->serial != chunk->serial || c->x != chunk->x || c->z != chunk->z) {
            free_column(c);
            c->present = 1; c->x = chunk->x; c->z = chunk->z; c->serial = chunk->serial;
            mc_mesh_heightmap(w, chunk, c->height); c->has_height = 1;
            mark_all(c); w->dirty[i] = 0;
            /* Neighbor borders, occlusion and skylight depend on this column. */
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                Column *n = (dx || dz) ? column_at(w, c->x+dx, c->z+dz) : NULL;
                if (n) mark_all(n);
            }
        } else if (w->dirty[i]) {
            McHeightmap before; memcpy(before, c->height, sizeof(before));
            mc_mesh_heightmap(w, chunk, c->height);
            if (memcmp(before, c->height, sizeof(before))) {
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                    Column *n = column_at(w, c->x+dx, c->z+dz);
                    if (n) mark_all(n);
                }
            }
            for (int s = 0; s < SECTIONS; s++) if (w->dirty[i] & (1u << s)) c->s[s].dirty = 1;
            w->dirty[i] = 0;
        }
    }
    for (int i = 0; i < MC_CACHE_COUNT; i++) {
        Column *c = &columns[i]; uint16_t mask = 0; int bit = 0;
        if (!c->present) continue;
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++)
            if (dx || dz) { if (mc_world_chunk(w, c->x+dx, c->z+dz)) mask |= 1u << bit; bit++; }
        if (mask != c->neighbors) { c->neighbors = mask; mark_all(c); }
    }
    /* Upload finished meshes whose column has not been replaced meanwhile. */
    stats.meshed = 0;
    for (int i = 0; i < RESULTS; i++) {
        Result *r = &results[i];
        if (r->state != RESULT_READY) continue;
        Column *c = &columns[r->slot];
        if (c->present && c->serial == r->serial) { upload(&c->s[r->sy], &r->mesh); stats.meshed++; }
        r->state = RESULT_FREE;
    }
    int unused; next_dirty(&unused, &stats.pending);
    stats.mesh_ms = (sceKernelGetProcessTimeWide() - start)/1000.0f;
    stats.build_ms = built_sections ? build_us/1000.0f/built_sections : 0;
}

void mc_gl_begin(void) {
    overlay_count = 0;
    glViewport(0, 0, 960, 544);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

typedef struct { float m[16]; } Mat;
static Mat multiply(const Mat *a, const Mat *b) {
    Mat r;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a->m[i*4+k]*b->m[k*4+j];
        r.m[i*4+j] = s;
    }
    return r;
}
static void sky(const McCamera *camera, const Mat *mvp) {
    /* Project a far point on the horizon in the view direction to find its screen row. */
    float yaw = camera->yaw*(PI/180);
    float p[4] = {-sinf(yaw)*100, 0, cosf(yaw)*100, 1}, clip[4];
    for (int i = 0; i < 4; i++) clip[i] = mvp->m[i*4]*p[0] + mvp->m[i*4+1]*p[1] + mvp->m[i*4+2]*p[2] + mvp->m[i*4+3];
    float horizon = clip[3] > 0.001f ? (1 - clip[1]/clip[3])*272 : (camera->pitch > 0 ? -1000 : 2000);
    uint32_t top = 0xff000000 | SKY_TOP, low = 0xff000000 | SKY_HORIZON;
    float band = 260, top_y = horizon - band;
    OverlayVertex *v = overlay + overlay_count*6;
    if (overlay_count + 2 > OVERLAY_MAX) return;
    if (top_y > 0) { mc_gl_rect(0, 0, 960, top_y, top); v = overlay + overlay_count*6; }
    if (overlay_count + 1 > OVERLAY_MAX) return;
    /* Gradient band: two vertices top, two at the horizon. */
    uint32_t a = abgr(top), b = abgr(low);
    OverlayVertex q0 = {0, top_y, 0.02f, 0.02f, a}, q1 = {0, horizon, 0.02f, 0.02f, b}, q2 = {960, horizon, 0.02f, 0.02f, b}, q3 = {960, top_y, 0.02f, 0.02f, a};
    v[0] = q0; v[1] = q1; v[2] = q2; v[3] = q0; v[4] = q2; v[5] = q3; overlay_count++;
}
static void flush_overlay(void) {
    if (!overlay_count) return;
    glUseProgram(overlay_program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, font);
    glUniform1i(overlay_tex, 0);
    glUniform2f(overlay_screen, 2.0f/960, 2.0f/544);
    const GLint *a = attribs[2];
    glEnableVertexAttribArray(a[0]); glEnableVertexAttribArray(a[1]); glEnableVertexAttribArray(a[2]);
    glVertexAttribPointer(a[0], 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), &overlay[0].x);
    glVertexAttribPointer(a[1], 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), &overlay[0].u);
    glVertexAttribPointer(a[2], 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OverlayVertex), &overlay[0].color);
    glDrawArrays(GL_TRIANGLES, 0, overlay_count*6);
    overlay_count = 0;
}

typedef struct { Column *c; int s; float d; } Visible;
static Visible visible[MC_CACHE_COUNT*SECTIONS];
static int by_distance(const void *a, const void *b) {
    float x = ((const Visible *)a)->d, y = ((const Visible *)b)->d;
    return x < y ? -1 : x > y;
}
static void bind_terrain(int which, const Mat *mvp) {
    TerrainUniforms *u = &uniforms[which];
    glUseProgram(which ? cutout_program : terrain_program);
    for (int i = 0; i < 4; i++) glUniform4fv(u->rows[i], 1, mvp->m + i*4);
    glUniform2f(u->fog, FOG_START, 1.0f/(FOG_END-FOG_START));
    glUniform4f(u->fog_color, ((SKY_HORIZON >> 16) & 255)/255.0f, ((SKY_HORIZON >> 8) & 255)/255.0f, (SKY_HORIZON & 255)/255.0f, 1);
    glUniform1i(u->tex, 0);
}
static void draw_pass(int pass, int which, unsigned count, const McCamera *camera, int reverse) {
    TerrainUniforms *u = &uniforms[which];
    for (unsigned k = 0; k < count; k++) {
        Visible *v = &visible[reverse ? count-1-k : k];
        Section *s = &v->c->s[v->s];
        if (!s->quads[pass]) continue;
        unsigned first = 0;
        for (int p = 0; p < pass; p++) first += s->quads[p];
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
        const GLint *a = attribs[which];
        glVertexAttribPointer(a[0], 3, GL_SHORT, GL_FALSE, sizeof(McVertex), (void *)0);
        glVertexAttribPointer(a[1], 2, GL_SHORT, GL_FALSE, sizeof(McVertex), (void *)8);
        glVertexAttribPointer(a[2], 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(McVertex), (void *)12);
        glUniform3f(u->offset, (float)(v->c->x*16 - camera->x), (float)(world_min_y + v->s*16 - camera->y), (float)(v->c->z*16 - camera->z));
        glDrawElements(GL_TRIANGLES, s->quads[pass]*6, GL_UNSIGNED_SHORT, (void *)(uintptr_t)(first*6*sizeof(uint16_t)));
        stats.drawn_quads += s->quads[pass];
    }
}
void mc_gl_draw_world(const McCamera *camera, int min_y, int height) {
    (void)min_y; (void)height;
    float yaw = camera->yaw*(PI/180), pitch = camera->pitch*(PI/180);
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    float f[3] = {-sy*cp, -sp, cy*cp}, r[3] = {-cy, 0, -sy}, u[3] = {-sy*sp, cp, cy*sp};
    Mat view = {{r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, -f[0], -f[1], -f[2], 0, 0, 0, 0, 1}};
    float fovy = 70*(PI/180), aspect = 960.0f/544, near = 0.05f, far = 256.0f, t = 1/tanf(fovy/2);
    Mat proj = {{t/aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, (far+near)/(near-far), 2*far*near/(near-far), 0, 0, -1, 0}};
    Mat mvp = multiply(&proj, &view);
    sky(camera, &mvp);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    flush_overlay();

    /* Frustum planes from the clip matrix; a section is culled if fully outside one. */
    float planes[4][4];
    for (int i = 0; i < 4; i++) {
        int row = i/2; float sign = i % 2 ? -1 : 1;
        for (int k = 0; k < 4; k++) planes[i][k] = mvp.m[12+k] + sign*mvp.m[row*4+k];
    }
    unsigned count = 0; stats.sections = 0; stats.drawn_quads = 0;
    float limit = FOG_END + 12;
    for (int i = 0; i < MC_CACHE_COUNT; i++) {
        Column *c = &columns[i];
        if (!c->present) continue;
        float ox = (float)(c->x*16 - camera->x), oz = (float)(c->z*16 - camera->z);
        float nx = ox < 0 ? (ox+16 > 0 ? 0 : ox+16) : ox, nz = oz < 0 ? (oz+16 > 0 ? 0 : oz+16) : oz;
        if (nx*nx + nz*nz > limit*limit) continue;
        for (int s = 0; s < sections_per_column; s++) {
            Section *sec = &c->s[s];
            if (!sec->vbo) continue;
            stats.sections++;
            float oy = (float)(world_min_y + s*16 - camera->y), lo[3] = {ox, oy, oz}, inside = 1;
            for (int p = 0; p < 4 && inside; p++) {
                float x = planes[p][0] >= 0 ? lo[0]+16 : lo[0], y = planes[p][1] >= 0 ? lo[1]+16 : lo[1], z = planes[p][2] >= 0 ? lo[2]+16 : lo[2];
                if (planes[p][0]*x + planes[p][1]*y + planes[p][2]*z + planes[p][3] < 0) inside = 0;
            }
            if (!inside) continue;
            float cx = ox+8, cy2 = oy+8, cz = oz+8;
            visible[count++] = (Visible){c, s, cx*cx + cy2*cy2 + cz*cz};
        }
    }
    stats.visible = count;
    qsort(visible, count, sizeof(Visible), by_distance);

    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, atlas);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_indices);
    glEnableVertexAttribArray(attribs[0][0]); glEnableVertexAttribArray(attribs[0][1]); glEnableVertexAttribArray(attribs[0][2]);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    bind_terrain(0, &mvp);
    draw_pass(MC_PASS_OPAQUE, 0, count, camera, 0);
    glDisable(GL_CULL_FACE);
    bind_terrain(1, &mvp);
    draw_pass(MC_PASS_CUTOUT, 1, count, camera, 0);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    bind_terrain(0, &mvp);
    draw_pass(MC_PASS_TRANSLUCENT, 0, count, camera, 1);
    glDepthMask(GL_TRUE);
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
}
void mc_gl_end(void) {
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    flush_overlay();
    glDisable(GL_BLEND);
    vglSwapBuffers(GL_FALSE);
}
const McGlStats *mc_gl_stats(void) { return &stats; }
