#include "player.h"
#include <math.h>
#include <string.h>
#define EPS 0.0000001
#define HALF 0.3
#define HEIGHT 1.8
typedef struct { unsigned offset,count; } McShape;
#include "collision_data.inc"
const McBlockBox *mc_block_boxes(uint16_t state,unsigned *count) {
    unsigned index = state < sizeof(state_shapes)/sizeof(state_shapes[0]) ? state_shapes[state] : MC_FALLBACK_SHAPE;
    const McShape *s = &shapes[index]; *count = s->count; return block_boxes+s->offset;
}
void mc_player_reset(McPlayer *p,double x,double y,double z,float yaw,float pitch) {
    memset(p,0,sizeof(*p)); p->x=x;p->y=y;p->z=z;p->yaw=yaw;p->pitch=pitch;p->initialized=1;p->speed_scale=1;
}
typedef struct { double low[3],high[3]; } Aabb;
static Aabb bounds(const McPlayer *p) { return (Aabb){{p->x-HALF,p->y,p->z-HALF},{p->x+HALF,p->y+HEIGHT,p->z+HALF}}; }
static void offset(Aabb *b,unsigned axis,double delta) { b->low[axis]+=delta;b->high[axis]+=delta; }
static int loaded(const McWorld *w,const Aabb *b) {
    if (!w->ready) return 0;
    int x0=(int)floor(b->low[0]/16),x1=(int)floor((b->high[0]-EPS)/16);
    int z0=(int)floor(b->low[2]/16),z1=(int)floor((b->high[2]-EPS)/16);
    for (int z=z0;z<=z1;z++) for (int x=x0;x<=x1;x++) if (!mc_world_chunk(w,x,z)) return 0;
    return 1;
}
static double clip_box(const Aabb *moving,const Aabb *solid,unsigned axis,double delta) {
    for (unsigned i=0;i<3;i++) if (i!=axis && (moving->high[i]<=solid->low[i]+EPS || moving->low[i]>=solid->high[i]-EPS)) return delta;
    if (delta>0 && moving->high[axis]<=solid->low[axis]+EPS) {
        double gap=solid->low[axis]-moving->high[axis]; if (gap<delta) delta=gap>0?gap:0;
    } else if (delta<0 && moving->low[axis]>=solid->high[axis]-EPS) {
        double gap=solid->high[axis]-moving->low[axis]; if (gap>delta) delta=gap<0?gap:0;
    }
    return delta;
}
static double clip(const McWorld *w,const Aabb *b,unsigned axis,double delta) {
    Aabb sweep=*b;
    if(delta>0)sweep.high[axis]+=delta;else sweep.low[axis]+=delta;
    int low[3],high[3];
    for(unsigned i=0;i<3;i++){low[i]=(int)floor(sweep.low[i])-1;high[i]=(int)floor(sweep.high[i])+1;}
    for(int y=low[1];y<=high[1];y++)for(int z=low[2];z<=high[2];z++)for(int x=low[0];x<=high[0];x++) {
        if(y<w->min_y || y>=w->min_y+w->height)continue;
        unsigned count;const McBlockBox *boxes=mc_block_boxes(mc_world_get(w,x,y,z),&count);
        for(unsigned n=0;n<count;n++){
            Aabb s;double origin[3]={x,y,z};
            for(unsigned i=0;i<3;i++){s.low[i]=origin[i]+boxes[n].bounds[i]/32.0;s.high[i]=origin[i]+boxes[n].bounds[i+3]/32.0;}
            delta=clip_box(b,&s,axis,delta);
        }
    }
    return delta;
}
void mc_player_tick(McPlayer *p,const McWorld *w,const McInput *in) {
    if(!p->initialized)return;
    p->yaw=in->yaw;p->pitch=in->pitch;p->ticks++;
    Aabb original=bounds(p);p->waiting=!loaded(w,&original);
    if(p->waiting)return;
    p->grounded=clip(w,&original,1,-0.001)>-0.001+EPS;
    if(p->jump_cooldown>0)p->jump_cooldown--;
    float forward=in->forward,right=in->right;
    float size=sqrtf(forward*forward+right*right);if(size>1){forward/=size;right/=size;}
    double yaw=p->yaw*(3.14159265358979323846/180);
    double speed=p->flying?0.25:0.21585*p->speed_scale;
    double dx=(-sin(yaw)*forward-cos(yaw)*right)*speed+p->velocity_x;
    double dz=(cos(yaw)*forward-sin(yaw)*right)*speed+p->velocity_z;
    if(p->flying){p->velocity_y=in->vertical*0.25;p->grounded=0;}
    else if(in->jump && p->grounded && p->jump_cooldown==0){p->velocity_y=0.42;p->grounded=0;p->jump_cooldown=10;}
    double dy=p->velocity_y;
    Aabb target=original;
    if(dx>0)target.high[0]+=dx;else target.low[0]+=dx;
    if(dz>0)target.high[2]+=dz;else target.low[2]+=dz;
    /* A missing column is a streaming boundary, not empty terrain. */
    if(!loaded(w,&target)){p->waiting=1;p->velocity_x=p->velocity_y=p->velocity_z=0;return;}
    Aabb b=original;
    double cy=clip(w,&b,1,dy);offset(&b,1,cy);
    double cx=clip(w,&b,0,dx);offset(&b,0,cx);
    double cz=clip(w,&b,2,dz);offset(&b,2,cz);
    int ground=(p->grounded && dy<=0) || (dy<0 && cy>dy+EPS);
    if(!p->flying && ground && (fabs(cx-dx)>EPS || fabs(cz-dz)>EPS)){
        /* Try a vanilla-sized step up (slabs/stairs), then settle on its top. */
        Aabb step=original;double up=clip(w,&step,1,0.6);offset(&step,1,up);
        double sx=clip(w,&step,0,dx);offset(&step,0,sx);
        double sz=clip(w,&step,2,dz);offset(&step,2,sz);
        double down=clip(w,&step,1,-up);offset(&step,1,down);
        if(sx*sx+sz*sz>cx*cx+cz*cz+EPS){b=step;cx=sx;cz=sz;cy=up+down;ground=down>-up+EPS;}
    }
    p->x=(b.low[0]+b.high[0])*0.5;p->y=b.low[1];p->z=(b.low[2]+b.high[2])*0.5;
    p->grounded=!p->flying && (ground || clip(w,&b,1,-0.001)>-0.001+EPS);
    if(fabs(cy-dy)>EPS || p->grounded)p->velocity_y=0;
    if(!p->flying)p->velocity_y=(p->velocity_y-0.08)*0.98;
    p->velocity_x=fabs(cx-dx)>EPS?0:p->velocity_x*(p->grounded?.546:.91);
    p->velocity_z=fabs(cz-dz)>EPS?0:p->velocity_z*(p->grounded?.546:.91);
}
