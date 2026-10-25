#ifndef MC_GL_RENDER_H
#define MC_GL_RENDER_H
#include "world.h"
#include "render.h"
#include <stdint.h>
/* Hardware renderer for the Vita (vitaGL). Call from the main thread only. */
typedef struct {
    unsigned sections, visible, drawn_quads, meshed, pending, dropped;
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
void mc_gl_draw_world(const McCamera *camera, int min_y, int height);
/* 2D overlay in 960x544 pixel coordinates. Colors are 0xAARRGGBB. */
void mc_gl_rect(float x, float y, float w, float h, uint32_t color);
void mc_gl_text(float x, float y, float scale, uint32_t color, const char *text);
void mc_gl_textf(float x, float y, float scale, uint32_t color, const char *format, ...);
float mc_gl_text_width(const char *text, float scale);
void mc_gl_end(void);
const McGlStats *mc_gl_stats(void);
#endif
