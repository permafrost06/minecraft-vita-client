#ifndef MC_RENDER_H
#define MC_RENDER_H
#include "world.h"
#define MC_VIEW_W 240
#define MC_VIEW_H 136
typedef struct { double x,y,z; float yaw,pitch; } McCamera;
void mc_render_init(void);
/* Output uses Vita RGBA8 memory byte order. Caller locks the world while drawing. */
void mc_render(const McWorld *world,const McCamera *camera,uint32_t pixels[MC_VIEW_W*MC_VIEW_H]);
void mc_render_preview(const McWorld *world,const McCamera *camera,uint32_t *pixels,int stride);
float mc_camera_axis(unsigned char raw);
void mc_camera_look(McCamera *camera,float dx,float dy);
void mc_camera_move(McCamera *camera,float forward,float right,float vertical,float seconds);
#endif
