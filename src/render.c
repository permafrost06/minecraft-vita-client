#include "render.h"
#include <math.h>
#include <stdlib.h>
#define PI 3.14159265358979323846f
#define FAR 64.0f
typedef struct { unsigned low,high; uint32_t side,top; unsigned style; } Range;
static const Range ranges[] = {
#include "materials.inc"
};
static uint32_t side_colors[32768],top_colors[32768];
static uint8_t styles[32768];
static uint32_t rgba(uint32_t rgb) { return 0xff000000 | ((rgb & 255) << 16) | (rgb & 0xff00) | (rgb >> 16); }
void mc_render_init(void) {
    for (unsigned i = 0; i < 32768; i++) { side_colors[i] = top_colors[i] = 0xff999999; styles[i] = 1; }
    for (unsigned i = 0; i < sizeof(ranges)/sizeof(ranges[0]); i++) {
        const Range *r = &ranges[i];
        for (unsigned v = r->low; v <= r->high && v < 32768; v++) {
            side_colors[v] = rgba(r->side); top_colors[v] = rgba(r->top); styles[v] = r->style;
        }
    }
}
void mc_camera_look(McCamera *c,float dx,float dy) {
    c->yaw = fmodf(c->yaw+dx,360.0f); c->pitch += dy;
    if (c->pitch < -85) c->pitch = -85;
    if (c->pitch > 85) c->pitch = 85;
}
void mc_camera_move(McCamera *c,float forward,float right,float vertical,float seconds) {
    float yaw = c->yaw*(PI/180), speed = 5.0f*seconds;
    c->x += (-sinf(yaw)*forward+cosf(yaw)*right)*speed;
    c->z += (cosf(yaw)*forward+sinf(yaw)*right)*speed;
    c->y += vertical*speed;
}
static uint32_t blend(uint32_t color,uint32_t sky,float light,float fog) {
    unsigned out = 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        float a = ((color >> shift)&255)*light;
        unsigned b = (sky >> shift)&255;
        unsigned v = a*(1-fog)+b*fog;
        out |= (v > 255 ? 255 : v) << shift;
    }
    return out;
}
static uint32_t ray(const McWorld *w,const McCamera *c,float dx,float dy,float dz,uint32_t sky) {
    int x = (int)floor(c->x), y = (int)floor(c->y), z = (int)floor(c->z);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, sz = dz < 0 ? -1 : 1;
    float tx = fabsf(dx) > 0.00001f ? fabsf(1/dx) : 1e9f;
    float ty = fabsf(dy) > 0.00001f ? fabsf(1/dy) : 1e9f;
    float tz = fabsf(dz) > 0.00001f ? fabsf(1/dz) : 1e9f;
    float mx = (sx > 0 ? x+1-c->x : c->x-x)*tx;
    float my = (sy > 0 ? y+1-c->y : c->y-y)*ty;
    float mz = (sz > 0 ? z+1-c->z : c->z-z)*tz;
    float distance = 0; int face = 1;
    for (unsigned step = 0; step < 160 && distance < FAR; step++) {
        uint16_t state = mc_world_get(w,x,y,z); unsigned v = state < 32768 ? state : 1;
        if (styles[v]) {
            uint32_t color = face == 1 && sy < 0 ? top_colors[v] : side_colors[v];
            float hx = c->x+dx*distance, hy = c->y+dy*distance, hz = c->z+dz*distance;
            float u = face == 0 ? hz : hx, vertical = face == 1 ? hz : hy;
            unsigned a = (unsigned)(int)floorf(u*16), b = (unsigned)(int)floorf(vertical*16);
            uint32_t noise = a*374761393u+b*668265263u+state*2246822519u; noise = (noise^(noise >> 13))*1274126177u;
            float shade = 0.91f+((noise >> 28)&15)*0.008f;
            shade *= face == 1 ? 1.0f : face == 0 ? 0.78f : 0.64f;
            if ((a & 15) == 0 || (b & 15) == 0) shade *= 0.92f;
            if (styles[v] == 3) shade = 1.0f;
            float fog = distance/FAR; fog = fog*fog*0.82f;
            return blend(color,sky,shade,fog);
        }
        if (mx < my && mx < mz) { distance = mx; mx += tx; x += sx; face = 0; }
        else if (my < mz) { distance = my; my += ty; y += sy; face = 1; }
        else { distance = mz; mz += tz; z += sz; face = 2; }
    }
    return sky;
}
void mc_render(const McWorld *w,const McCamera *c,uint32_t pixels[MC_VIEW_W*MC_VIEW_H]) {
    float yaw = c->yaw*(PI/180), pitch = c->pitch*(PI/180);
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    float fx = -sy*cp, fy = -sp, fz = cy*cp;
    float rx = cy, rz = sy, ux = -sy*sp, uy = cp, uz = cy*sp;
    /* 75-degree horizontal FOV, deliberately low resolution for the first renderer. */
    float scale = 0.767327f;
    for (int y = 0; y < MC_VIEW_H; y++) {
        float py = (1-2*(y+0.5f)/MC_VIEW_H)*scale*MC_VIEW_H/MC_VIEW_W;
        uint32_t sky = rgba(0x84b5df);
        for (int x = 0; x < MC_VIEW_W; x++) {
            float px = (2*(x+0.5f)/MC_VIEW_W-1)*scale;
            float dx = fx+rx*px+ux*py, dy = fy+uy*py, dz = fz+rz*px+uz*py;
            float inv = 1/sqrtf(dx*dx+dy*dy+dz*dz);
            pixels[y*MC_VIEW_W+x] = ray(w,c,dx*inv,dy*inv,dz*inv,sky);
        }
    }
    for (int i = -3; i <= 3; i++) {
        pixels[(MC_VIEW_H/2)*MC_VIEW_W+MC_VIEW_W/2+i] = 0xffeeeeee;
        pixels[(MC_VIEW_H/2+i)*MC_VIEW_W+MC_VIEW_W/2] = 0xffeeeeee;
    }
}
