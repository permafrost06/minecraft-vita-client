#include "environment.h"
#include <math.h>
#include <string.h>
void mc_environment_init(McEnvironment *e){memset(e,0,sizeof(*e));e->has_sky=1;e->time=6000;}
double mc_environment_time(const McEnvironment *e,uint64_t now){
    return fmod((double)(e->time%24000)+(e->known && e->ticking && !e->fixed && now>e->received?(now-e->received)/50.0:0),24000);
}
static uint32_t tint(uint32_t color,float daylight,float rain){
    float r=(color>>16)&255,g=(color>>8)&255,b=color&255;
    float gray=(r*.3f+g*.59f+b*.11f)*.65f;
    r=(r*(1-rain*.7f)+gray*rain*.7f)*daylight;
    g=(g*(1-rain*.7f)+gray*rain*.7f)*daylight;
    b=(b*(1-rain*.7f)+gray*rain*.7f)*daylight;
    return ((unsigned)r<<16)|((unsigned)g<<8)|(unsigned)b;
}
McLighting mc_environment_lighting(const McEnvironment *e,uint64_t now){
    /* Match the broad vanilla celestial curve; the procedural sky has no biome tint. */
    double angle=mc_environment_time(e,now)/24000.0-.25;
    angle-=floor(angle);double eased=(1-cos(angle*3.141592653589793))/2;
    angle=angle+(eased-angle)/3;
    float day=(float)(cos(angle*6.283185307179586)*2+.5);
    if(day<0)day=0;
    if(day>1)day=1;
    float sky=(.2f+.8f*day)*(1-e->rain*.3125f)*(1-e->thunder*.3125f);
    McLighting l={e->has_sky?sky:0,e->ambient,0,0};
    float visible=.07f+.93f*day;
    l.top=tint(0x5d8fe0,visible,e->rain);l.horizon=tint(0xb4d0f0,visible,e->rain);
    if(!e->has_sky){l.top=l.horizon=e->ambient>.01f?0x301818:0x181520;}
    return l;
}
float mc_light_brightness(float sky,float block,const McLighting *l){
    float level=fmaxf(sky*l->sky,block);float value=.1f+.9f*level/(4-3*level);
    return value+(1-value)*l->ambient;
}
