#ifndef MC_GL_RENDER_H
#define MC_GL_RENDER_H
#include "world.h"
#include "render.h"
#include "entities.h"
#include <stdint.h>
/* Hardware renderer for the Vita (vitaGL). Call from the main thread only. */
typedef struct {
    unsigned sections, visible, drawn_quads, meshed, pending, dropped, entities, entity_vertices;
    float mesh_ms, build_ms;
} McGlStats;
int mc_gl_init(void);
/* Starts the background mesher. It locks `mutex` around world access. */
int mc_gl_start_worker(McWorld *world, int mutex);
void mc_gl_term(void);
/* Tracks world changes and uploads finished meshes. The caller locks the world. */
void mc_gl_update(McWorld *world, const McCamera *camera, unsigned budget_us);
void mc_gl_forget(void);
void mc_gl_begin(void);
/* view_distance is the server's chunk radius; fog hides the edge of loaded terrain. */
void mc_gl_draw_world(const McCamera *camera, int view_distance,const McEntities *entities,const McLighting *lighting,uint64_t now);
/* 2D overlay in 960x544 pixel coordinates. Colors are 0xAARRGGBB. */
void mc_gl_rect(float x, float y, float w, float h, uint32_t color);
void mc_gl_item_icon(int item,float x,float y,float size);
void mc_gl_text(float x, float y, float scale, uint32_t color, const char *text);
void mc_gl_textf(float x, float y, float scale, uint32_t color, const char *format, ...);
float mc_gl_text_width(const char *text, float scale);
void mc_gl_end(void);
const McGlStats *mc_gl_stats(void);
#endif
