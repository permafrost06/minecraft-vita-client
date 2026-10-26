#ifndef MC_ENVIRONMENT_H
#define MC_ENVIRONMENT_H
#include <stdint.h>
typedef struct {
    uint64_t time, received;
    int ticking, known, has_sky, fixed;
    float ambient, rain, thunder;
} McEnvironment;
typedef struct { float sky, ambient; uint32_t top, horizon; } McLighting;
void mc_environment_init(McEnvironment *e);
double mc_environment_time(const McEnvironment *e,uint64_t now);
McLighting mc_environment_lighting(const McEnvironment *e,uint64_t now);
float mc_light_brightness(float sky,float block,const McLighting *l);
#endif
