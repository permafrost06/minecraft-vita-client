#include "player.h"
#include "game_data.h"
#include "net.h"
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
    memset(p,0,sizeof(*p)); p->x=x;p->y=y;p->z=z;p->yaw=yaw;p->pitch=pitch;p->initialized=1;p->speed_scale=1;p->height=1.8f;p->eye=MC_PLAYER_EYE;
}
typedef struct { double low[3],high[3]; } Aabb;
static Aabb bounds(const McPlayer *p) { return (Aabb){{p->x-HALF,p->y,p->z-HALF},{p->x+HALF,p->y+p->height,p->z+HALF}}; }
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
static const McBlockBox *player_boxes(uint16_t state,int y,const McPlayer *p,unsigned *count){
    static const McBlockBox top={{0,28,0,32,32,32}},full={{0,0,0,32,32,32}};
    unsigned flags=mc_dig_def(state)->physics;
    if(flags&BP_SCAFFOLD){*count=!p->sneaking && p->y>=y+1-EPS;return &top;}
    if(flags&BP_POWDER){*count=p->leather_boots && !p->sneaking && p->y>=y+1-EPS;return &full;}
    return mc_block_boxes(state,count);
}
static double clip(const McWorld *w,const Aabb *b,unsigned axis,double delta,const McPlayer *p) {
    Aabb sweep=*b;
    if(delta>0)sweep.high[axis]+=delta;else sweep.low[axis]+=delta;
    int low[3],high[3];
    for(unsigned i=0;i<3;i++){low[i]=(int)floor(sweep.low[i])-1;high[i]=(int)floor(sweep.high[i])+1;}
    for(int y=low[1];y<=high[1];y++)for(int z=low[2];z<=high[2];z++)for(int x=low[0];x<=high[0];x++) {
        if(y<w->min_y || y>=w->min_y+w->height)continue;
        unsigned count;const McBlockBox *boxes=player_boxes(mc_world_get(w,x,y,z),y,p,&count);
        for(unsigned n=0;n<count;n++){
            Aabb s;double origin[3]={x,y,z};
            for(unsigned i=0;i<3;i++){s.low[i]=origin[i]+boxes[n].bounds[i]/32.0;s.high[i]=origin[i]+boxes[n].bounds[i+3]/32.0;}
            delta=clip_box(b,&s,axis,delta);
        }
    }
    return delta;
}
static int intersects(const McWorld *w,const Aabb *b,const McPlayer *p){
    for(int y=(int)floor(b->low[1])-1;y<=(int)floor(b->high[1]-EPS);y++)for(int z=(int)floor(b->low[2]);z<=(int)floor(b->high[2]-EPS);z++)for(int x=(int)floor(b->low[0]);x<=(int)floor(b->high[0]-EPS);x++){
        unsigned count;const McBlockBox *boxes=player_boxes(mc_world_get(w,x,y,z),y,p,&count);
        for(unsigned n=0;n<count;n++){
            double o[3]={x,y,z};int overlap=1;
            for(int a=0;a<3;a++)if(b->high[a]<=o[a]+boxes[n].bounds[a]/32.0+EPS || b->low[a]>=o[a]+boxes[n].bounds[a+3]/32.0-EPS)overlap=0;
            if(overlap)return 1;
        }
    }
    return 0;
}
static double fluid_top(const McWorld *w,int x,int y,int z,int kind){
    uint16_t state=mc_world_get(w,x,y,z);if(mc_block_fluid(state)!=kind)return y;
    if(mc_block_fluid(mc_world_get(w,x,y+1,z))==kind)return y+1;
    int level=mc_block_fluid_level(state);if(level>=8)level=0;
    return y+(8-level)/9.0;
}
static void environment(McPlayer *p,const McWorld *w){
    Aabb b=bounds(p);p->in_water=p->in_lava=p->climbing=p->scaffolding=0;
    for(int y=(int)floor(p->y-.01);y<=(int)floor(b.high[1]-EPS);y++)for(int z=(int)floor(b.low[2]+EPS);z<=(int)floor(b.high[2]-EPS);z++)for(int x=(int)floor(b.low[0]+EPS);x<=(int)floor(b.high[0]-EPS);x++){
        uint16_t state=mc_world_get(w,x,y,z);unsigned flags=mc_dig_def(state)->physics;int kind=mc_block_fluid(state);
        if(kind && fluid_top(w,x,y,z,kind)>p->y+.05){if(kind==1)p->in_water=1;else p->in_lava=1;}
        if(flags&BP_CLIMB){p->climbing=1;if(flags&BP_SCAFFOLD)p->scaffolding=1;}
    }
    int ex=(int)floor(p->x),ey=(int)floor(p->y+p->eye),ez=(int)floor(p->z);
    p->eyes_in_water=fluid_top(w,ex,ey,ez,1)>p->y+p->eye;
}
static void fluid_push(McPlayer *p,const McWorld *w,int kind){
    int x=(int)floor(p->x),y=(int)floor(p->y+.1),z=(int)floor(p->z),level=mc_block_fluid_level(mc_world_get(w,x,y,z));
    if(mc_block_fluid(mc_world_get(w,x,y,z))!=kind)return;
    int falling=level>=8;if(falling)level=0;
    double fx=0,fz=0;
    static const int dirs[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
    for(int i=0;i<4;i++){
        int nx=x+dirs[i][0],nz=z+dirs[i][1];uint16_t neighbor=mc_world_get(w,nx,y,nz);int difference=0;
        if(mc_block_fluid(neighbor)==kind){int n=mc_block_fluid_level(neighbor);difference=(n>=8?0:n)-level;}
        else{
            unsigned count;mc_block_boxes(neighbor,&count);
            if(!count && mc_block_fluid(mc_world_get(w,nx,y-1,nz))==kind){int n=mc_block_fluid_level(mc_world_get(w,nx,y-1,nz));difference=(n>=8?0:n)-level+8;}
        }
        fx+=dirs[i][0]*difference;fz+=dirs[i][1]*difference;
    }
    double length=hypot(fx,fz);if(length>EPS){double push=kind==1?.014:.007;p->velocity_x+=fx/length*push;p->velocity_z+=fz/length*push;}
    if(falling)p->velocity_y-=.014;
}
static int supported(const McWorld *w,Aabb b,double dx,double dz,const McPlayer *p){offset(&b,0,dx);offset(&b,2,dz);return clip(w,&b,1,-.6,p)>-.6+EPS;}
static double backoff(double delta){return fabs(delta)<.05?0:delta>0?delta-.05:delta+.05;}
static double border_clip(const McWorld *w,const Aabb *b,int axis,double delta){
    double half=mc_world_border_size(w,net_millis())/2,center=axis==0?w->border_x:w->border_z;
    if(delta>0){double gap=center+half-b->high[axis];if(gap<delta)delta=gap>0?gap:0;}
    else if(delta<0){double gap=center-half-b->low[axis];if(gap>delta)delta=gap<0?gap:0;}
    return delta;
}
void mc_player_tick(McPlayer *p,const McWorld *w,const McInput *in){
    if(!p->initialized)return;
    p->yaw=in->yaw;p->pitch=in->pitch;p->ticks++;
    p->sneaking=!!in->sneak;
    Aabb original=bounds(p);p->waiting=!loaded(w,&original);if(p->waiting)return;
    environment(p,w);
    p->sprinting=!!in->sprint && !p->sneaking && in->forward>.8f;
    p->swimming=!p->flying && p->in_water && p->eyes_in_water && p->sprinting;
    float height=p->swimming?.6f:p->sneaking&&!p->flying?1.5f:1.8f;
    Aabb resize=original;resize.high[1]=p->y+height;
    if(intersects(w,&resize,p)){height=1.5f;resize.high[1]=p->y+height;if(intersects(w,&resize,p))height=.6f;}
    p->height=height;p->eye=height<1?.4f:height<1.7f?1.27f:MC_PLAYER_EYE;
    original=bounds(p);environment(p,w);
    p->grounded=clip(w,&original,1,-.001,p)>-.001+EPS;
    if(p->jump_cooldown>0)p->jump_cooldown--;
    unsigned floor_flags=mc_dig_def(mc_world_get(w,(int)floor(p->x),(int)floor(p->y-.01),(int)floor(p->z)))->physics;
    double surface=(floor_flags&BP_BLUE_ICE)?.989:(floor_flags&BP_ICE)?.98:(floor_flags&BP_SLIME)?.8:.6;
    float forward=in->forward,right=in->right;
    if(!p->flying && (p->sneaking || (p->height<1 && !p->in_water))){forward*=.3f;right*=.3f;}
    float size=sqrtf(forward*forward+right*right);if(size>1){forward/=size;right/=size;}
    double yaw=p->yaw*(3.14159265358979323846/180),acceleration,drag;
    int liquid=!p->flying && (p->in_water || p->in_lava),was_grounded=p->grounded;
    double movement=.1*p->speed_scale*(p->sprinting?1.3:1);
    if(liquid){
        acceleration=.02;drag=p->in_water?(p->sprinting?.9:.8):.5;
        if(p->in_water){double depth=p->depth_strider>3?3:p->depth_strider;if(!p->grounded)depth*=.5;drag+=(.546-drag)*depth/3;acceleration+=(movement-acceleration)*depth/3;if(p->dolphins_grace)drag=.96;}
        fluid_push(p,w,p->in_water?1:2);
        if(in->jump)p->velocity_y+=.04;
        if(in->sneak)p->velocity_y-=.04;
        if(p->swimming && in->forward>0){double direction=-sin(in->pitch*.017453292519943295);p->velocity_y+=(direction-p->velocity_y)*.085;}
    }else{drag=p->grounded?surface*.91:.91;acceleration=p->grounded?movement*.216/(surface*surface*surface):.02*(p->sprinting?1.3:1);}
    double dx=p->velocity_x+(-sin(yaw)*forward-cos(yaw)*right)*acceleration*.98;
    double dz=p->velocity_z+(cos(yaw)*forward-sin(yaw)*right)*acceleration*.98;
    if(p->flying){dx=p->velocity_x+(-sin(yaw)*forward-cos(yaw)*right)*.25;dz=p->velocity_z+(cos(yaw)*forward-sin(yaw)*right)*.25;p->velocity_y=in->vertical*.25;p->grounded=0;drag=.6;}
    else if(!liquid && in->jump && p->grounded && !p->jump_cooldown){
        p->velocity_y=(floor_flags&BP_HONEY)?.168:.42;p->velocity_y+=.1*p->jump_boost;p->grounded=0;p->jump_cooldown=10;
        if(p->sprinting){dx-=sin(yaw)*.2;dz+=cos(yaw)*.2;}
    }
    if(p->climbing && !p->flying){
        dx=fmax(-.15,fmin(.15,dx));dz=fmax(-.15,fmin(.15,dz));p->velocity_y=fmax(-.15,p->velocity_y);
        if(p->scaffolding && in->sneak)p->velocity_y=-.15;
        else if(in->sneak && p->velocity_y<0)p->velocity_y=0;
        if(in->jump || (p->horizontal_collision && !in->sneak))p->velocity_y=.2;
    }
    if(p->sneaking && was_grounded && !p->flying && !liquid){
        while(dx && !supported(w,original,dx,0,p))dx=backoff(dx);
        while(dz && !supported(w,original,0,dz,p))dz=backoff(dz);
        while(dx && dz && !supported(w,original,dx,dz,p)){dx=backoff(dx);dz=backoff(dz);}
    }
    dx=border_clip(w,&original,0,dx);dz=border_clip(w,&original,2,dz);
    double dy=p->velocity_y;unsigned body_flags=mc_dig_def(mc_world_get(w,(int)floor(p->x),(int)floor(p->y+.1),(int)floor(p->z)))->physics;
    if((body_flags&BP_WEB) && !p->flying){dx*=.25;dz*=.25;dy*=.05;}
    Aabb target=original;if(dx>0)target.high[0]+=dx;else target.low[0]+=dx;if(dz>0)target.high[2]+=dz;else target.low[2]+=dz;
    if(!loaded(w,&target)){p->waiting=1;p->velocity_x=p->velocity_y=p->velocity_z=0;return;}
    Aabb b=original;double cy=clip(w,&b,1,dy,p);offset(&b,1,cy);double cx=clip(w,&b,0,dx,p);offset(&b,0,cx);double cz=clip(w,&b,2,dz,p);offset(&b,2,cz);
    int ground=(p->grounded && dy<=0) || (dy<0 && cy>dy+EPS);
    if(!p->flying && ground && (fabs(cx-dx)>EPS || fabs(cz-dz)>EPS)){
        Aabb step=original;double up=clip(w,&step,1,.6,p);offset(&step,1,up);double sx=clip(w,&step,0,dx,p);offset(&step,0,sx);double sz=clip(w,&step,2,dz,p);offset(&step,2,sz);double down=clip(w,&step,1,-up,p);offset(&step,1,down);
        if(sx*sx+sz*sz>cx*cx+cz*cz+EPS){b=step;cx=sx;cz=sz;cy=up+down;ground=down>-up+EPS;}
    }
    p->x=(b.low[0]+b.high[0])*.5;p->y=b.low[1];p->z=(b.low[2]+b.high[2])*.5;
    p->horizontal_collision=fabs(cx-dx)>EPS || fabs(cz-dz)>EPS;
    p->grounded=!p->flying && (ground || clip(w,&b,1,-.001,p)>-.001+EPS);
    floor_flags=mc_dig_def(mc_world_get(w,(int)floor(p->x),(int)floor(p->y-.01),(int)floor(p->z)))->physics;
    int bounce=dy<0 && cy>dy+EPS && !p->sneaking && (floor_flags&(BP_SLIME|BP_BED));
    if(bounce)p->velocity_y=-dy*((floor_flags&BP_BED)?.66:1);
    else if(fabs(cy-dy)>EPS || p->grounded)p->velocity_y=0;
    p->velocity_x=fabs(cx-dx)>EPS?0:dx*drag;p->velocity_z=fabs(cz-dz)>EPS?0:dz*drag;
    if(body_flags&BP_WEB)p->velocity_x=p->velocity_y=p->velocity_z=0;
    if(!p->flying){
        if(liquid)p->velocity_y=p->velocity_y*(p->in_water?.8:.5)-(p->in_water?.005:.02)*(p->slow_falling?.125:1);
        else if(p->levitation)p->velocity_y=(p->velocity_y+(.05*p->levitation-p->velocity_y)*.2)*.98;
        else p->velocity_y=(p->velocity_y-(p->slow_falling && p->velocity_y<=0?.01:.08))*.98;
        if((floor_flags&BP_HONEY) || ((floor_flags&BP_SOUL_SAND) && !p->soul_speed)){p->velocity_x*=.4;p->velocity_z*=.4;}
        if(body_flags&BP_BUBBLE){int down=mc_world_get(w,(int)floor(p->x),(int)floor(p->y+.1),(int)floor(p->z))==mc_dig_def(mc_world_get(w,(int)floor(p->x),(int)floor(p->y+.1),(int)floor(p->z)))->low;p->velocity_y=down?fmax(-.3,p->velocity_y-.03):fmin(.7,p->velocity_y+.06);}
    }
    if(fabs(p->velocity_x)<.003)p->velocity_x=0;
    if(fabs(p->velocity_z)<.003)p->velocity_z=0;
}
