#include "effects.h"
#include "blocks.h"
#include <math.h>
#include <string.h>
#include "particle_data.inc"
#define TYPES ((int)(sizeof(particle_styles)/sizeof(particle_styles[0])))
static int type_explosion,type_emitter,type_smoke,type_block,type_crit,type_magic,type_poof,type_flame,type_happy,type_dust,type_transition,type_falling_dust,type_marker,type_entity_effect,type_ambient_effect;
void mc_effects_init(McEffects *fx){
    memset(fx,0,sizeof(*fx));fx->rng=0x9e3779b9u;
    type_explosion=mc_particle_type("explosion");type_emitter=mc_particle_type("explosion_emitter");type_smoke=mc_particle_type("smoke");
    type_block=mc_particle_type("block");type_crit=mc_particle_type("crit");type_magic=mc_particle_type("enchanted_hit");
    type_poof=mc_particle_type("poof");type_flame=mc_particle_type("flame");type_happy=mc_particle_type("happy_villager");
    type_dust=mc_particle_type("dust");type_transition=mc_particle_type("dust_color_transition");type_falling_dust=mc_particle_type("falling_dust");
    type_marker=mc_particle_type("block_marker");type_entity_effect=mc_particle_type("entity_effect");type_ambient_effect=mc_particle_type("ambient_entity_effect");
}
int mc_particle_type(const char *name){for(int i=0;i<TYPES;i++)if(!strcmp(particle_styles[i].name,name))return i;return -1;}
const McParticleStyle *mc_particle_style(int type){return type>=0 && type<TYPES?&particle_styles[type]:NULL;}
static float uniform(McEffects *fx){fx->rng^=fx->rng<<13;fx->rng^=fx->rng>>17;fx->rng^=fx->rng<<5;return (fx->rng>>8)/16777216.0f;}
static float gaussian(McEffects *fx){float u=uniform(fx)+1e-7f,v=uniform(fx);return sqrtf(-2*logf(u))*cosf(6.2831853f*v);}
void mc_effects_spawn(McEffects *fx,int type,double x,double y,double z,float vx,float vy,float vz,uint32_t color){
    const McParticleStyle *s=mc_particle_style(type);
    if(!s || !s->life || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(vx) || !isfinite(vy) || !isfinite(vz))return;
    McParticle *p;
    if(fx->count<MC_PARTICLES)p=&fx->particles[fx->count++];
    else{p=&fx->particles[0];for(unsigned i=1;i<fx->count;i++)if(fx->particles[i].age*p->life>p->age*fx->particles[i].life)p=&fx->particles[i];fx->replaced++;}
    float jitter=.8f+.4f*uniform(fx);
    *p=(McParticle){x,y,z,fmaxf(-4,fminf(4,vx)),fmaxf(-4,fminf(4,vy)),fmaxf(-4,fminf(4,vz)),s->size*jitter,s->gravity,color?color:s->color,0,(uint16_t)(s->life*jitter+1)};
    fx->spawned++;
}
static void burst(McEffects *fx,int type,double x,double y,double z,int count,float spread,float speed,uint32_t color){
    for(int i=0;i<count;i++)mc_effects_spawn(fx,type,x+gaussian(fx)*spread,y+gaussian(fx)*spread,z+gaussian(fx)*spread,gaussian(fx)*speed,gaussian(fx)*speed+speed,gaussian(fx)*speed,color);
}
static void block_break(McEffects *fx,int bx,int by,int bz,uint16_t state){
    uint32_t color=mc_block_color(state);if(!color)return;
    /* Vanilla spawns 4x4x4 fragments; a 3x3x3 grid keeps the Vita budget. */
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)for(int k=0;k<3;k++){
        double x=bx+(i+.5)/3,y=by+(j+.5)/3,z=bz+(k+.5)/3;
        mc_effects_spawn(fx,type_block,x,y,z,(float)(x-bx-.5)*.15f,(float)(y-by-.5)*.15f+.08f,(float)(z-bz-.5)*.15f,color);
    }
}
static uint32_t rgb(float r,float g,float b){
    int R=(int)(fmaxf(0,fminf(1,r))*255),G=(int)(fmaxf(0,fminf(1,g))*255),B=(int)(fmaxf(0,fminf(1,b))*255);
    return (uint32_t)(R<<16|G<<8|B)|0x1000000; /* Bit 24 keeps black distinct from "style colour". */
}
static void position(uint64_t packed,int *x,int *y,int *z){
    *x=(int)(packed>>38);if(*x&0x2000000)*x-=0x4000000;
    *z=(int)((packed>>12)&0x3ffffff);if(*z&0x2000000)*z-=0x4000000;
    *y=(int)(packed&0xfff);if(*y&0x800)*y-=0x1000;
}
static int particle_packet(McEffects *fx,McReader *r){
    uint32_t type=mc_varint_read(r);mc_u8(r);
    double x=mc_f64(r),y=mc_f64(r),z=mc_f64(r);float ox=mc_f32(r),oy=mc_f32(r),oz=mc_f32(r),speed=mc_f32(r);int32_t count=(int32_t)mc_u32(r);
    if(r->error || type>=(uint32_t)TYPES || count<0 || !isfinite(ox) || !isfinite(oy) || !isfinite(oz) || !isfinite(speed))return -1;
    uint32_t color=0;
    if((int)type==type_block || (int)type==type_marker || (int)type==type_falling_dust){
        uint32_t state=mc_varint_read(r);if(r->error || state>65535 || r->pos!=r->n)return -1;
        color=mc_block_color((uint16_t)state);if(!color)return 0;
        color|=0x1000000;
    }else if((int)type==type_dust || (int)type==type_transition){
        float c[3];for(int i=0;i<3;i++)c[i]=mc_f32(r);mc_f32(r);
        if((int)type==type_transition)for(int i=0;i<3;i++)mc_f32(r);
        if(r->error || r->pos!=r->n)return -1;
        color=rgb(c[0],c[1],c[2]);
    }
    /* Remaining particle data (items, vibrations, sculk) only changes appearance and is ignored. */
    if(!count){
        if((int)type==type_entity_effect || (int)type==type_ambient_effect){mc_effects_spawn(fx,(int)type,x,y,z,0,.02f,0,rgb(ox,oy,oz));return 0;}
        mc_effects_spawn(fx,(int)type,x,y,z,ox*speed,oy*speed,oz*speed,color);return 0;
    }
    if(count>MC_PARTICLES_PER_PACKET)count=MC_PARTICLES_PER_PACKET;
    for(int i=0;i<count;i++)mc_effects_spawn(fx,(int)type,x+gaussian(fx)*ox,y+gaussian(fx)*oy,z+gaussian(fx)*oz,gaussian(fx)*speed,gaussian(fx)*speed,gaussian(fx)*speed,color);
    return 0;
}
int mc_effects_packet(McEffects *fx,const McEntities *entities,uint32_t id,const uint8_t *p,size_t n,uint64_t now){
    McReader r={p,n,0,0};
    if(id==0x28 || id==0x41){uint32_t rng=fx->rng;mc_effects_init(fx);fx->rng=rng;fx->last_tick=now;return 0;}
    if(id==0x26){fx->packets++;if(particle_packet(fx,&r)<0){strcpy(fx->error,"Invalid particle");return -1;}return 0;}
    if(id==0x1d){
        double x=mc_f64(&r),y=mc_f64(&r),z=mc_f64(&r);float strength=mc_f32(&r);uint32_t records=mc_varint_read(&r);
        if(r.error || records>(n-r.pos)/3 || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(strength)){strcpy(fx->error,"Invalid explosion");return -1;}
        fx->packets++;
        int big=strength>=2;
        burst(fx,big?type_emitter:type_explosion,x,y,z,big?10:4,big?strength*.35f:.3f,.02f,0);
        for(uint32_t i=0;i<records && i<24;i++){
            int dx=(int8_t)p[r.pos+i*3],dy=(int8_t)p[r.pos+i*3+1],dz=(int8_t)p[r.pos+i*3+2];
            mc_effects_spawn(fx,type_smoke,floor(x)+dx+.5,floor(y)+dy+.5,floor(z)+dz+.5,gaussian(fx)*.05f,.05f,gaussian(fx)*.05f,0);
        }
        return 0;
    }
    if(id==0x25){
        int32_t event=(int32_t)mc_u32(&r);uint64_t packed=mc_u64(&r);int32_t data=(int32_t)mc_u32(&r);mc_u8(&r);
        if(r.error || r.pos!=n){strcpy(fx->error,"Invalid world event");return -1;}
        int x,y,z;position(packed,&x,&y,&z);
        if(event==2001 && data>=0 && data<65536)block_break(fx,x,y,z,(uint16_t)data);
        else if(event==2000)burst(fx,type_smoke,x+.5,y+.5,z+.5,6,.15f,.02f,0);
        else if(event==2004){burst(fx,type_smoke,x+.5,y+.5,z+.5,12,.3f,.01f,0);burst(fx,type_flame,x+.5,y+.5,z+.5,8,.3f,.01f,0);}
        else if(event==2005 || event==1505)burst(fx,type_happy,x+.5,y+.5,z+.5,12,.35f,.01f,0);
        return 0;
    }
    if(id==0x07){
        int entity=(int)mc_varint_read(&r);uint64_t packed=mc_u64(&r);unsigned stage=mc_u8(&r);
        if(r.error || r.pos!=n){strcpy(fx->error,"Invalid destroy stage");return -1;}
        int x,y,z;position(packed,&x,&y,&z);
        McBreakStage *slot=NULL;
        for(int i=0;i<MC_BREAK_STAGES;i++){McBreakStage *s=&fx->stages[i];if(s->active && s->entity==entity){slot=s;break;}if(!s->active && !slot)slot=s;}
        if(stage>9){if(slot && slot->active && slot->entity==entity)slot->active=0;return 0;}
        if(slot)*slot=(McBreakStage){1,entity,x,y,z,(int)stage};
        return 0;
    }
    if((id==0x04 || id==0x1c) && entities){
        int key;unsigned value;
        if(id==0x04){key=(int)mc_varint_read(&r);value=mc_u8(&r);}else{key=(int32_t)mc_u32(&r);value=mc_u8(&r);}
        if(r.error || r.pos!=n)return 0; /* Entity tracking reports malformed packets. */
        const McEntity *e=mc_entity_get(entities,key);if(!e)return 0;
        double pos[3];mc_entity_position(e,now,pos);float h=mc_entity_def(e->type)->height;
        if(id==0x04 && (value==4 || value==5))burst(fx,value==4?type_crit:type_magic,pos[0],pos[1]+h*.5,pos[2],12,.3f,.08f,0);
        else if(id==0x1c && value==3)burst(fx,type_poof,pos[0],pos[1]+h*.5,pos[2],10,.3f,.02f,0);
    }
    return 0;
}
void mc_effects_update(McEffects *fx,uint64_t now){
    if(!fx->last_tick || now<fx->last_tick){fx->last_tick=now;return;}
    unsigned ticks=(unsigned)((now-fx->last_tick)/50);if(!ticks)return;
    fx->last_tick+=ticks*50ull;if(ticks>10)ticks=10;
    for(unsigned t=0;t<ticks;t++){
        unsigned kept=0;
        for(unsigned i=0;i<fx->count;i++){
            McParticle q=fx->particles[i];
            if(++q.age>=q.life)continue;
            q.vy-=q.gravity;q.x+=q.vx;q.y+=q.vy;q.z+=q.vz;q.vx*=.96f;q.vy*=.96f;q.vz*=.96f;
            fx->particles[kept++]=q;
        }
        fx->count=kept;
    }
}
static uint32_t vertex_color(uint32_t rgb,float alpha){
    unsigned a=(unsigned)(fmaxf(0,fminf(1,alpha))*255);
    return (a<<24)|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);
}
unsigned mc_effects_particles(const McEffects *fx,const double camera[3],const float right[3],const float up[3],McEntityVertex *out,unsigned capacity){
    unsigned n=0;
    for(unsigned i=0;i<fx->count && n+6<=capacity;i++){
        const McParticle *q=&fx->particles[i];
        float cx=(float)(q->x-camera[0]),cy=(float)(q->y-camera[1]),cz=(float)(q->z-camera[2]);
        float d2=cx*cx+cy*cy+cz*cz;
        if(d2>64*64 || d2<.16f)continue; /* Particles at the eye would cover the screen. */
        float fade=q->life>4 && q->age>q->life*3/4?(float)(q->life-q->age)/(q->life/4.0f):1,s=q->size*.5f;
        uint32_t color=vertex_color(q->color,fade);
        float corner[4][3];
        for(int c=0;c<4;c++){float a=(c&1)?s:-s,b=(c&2)?s:-s;for(int k=0;k<3;k++)corner[c][k]=(k==0?cx:k==1?cy:cz)+right[k]*a+up[k]*b;}
        static const int order[6]={0,1,3,0,3,2};
        for(int j=0;j<6;j++){const float *v=corner[order[j]];out[n++]=(McEntityVertex){v[0],v[1],v[2],color};}
    }
    return n;
}
unsigned mc_effects_cracks(const McEffects *fx,const double camera[3],McEntityVertex *out,unsigned capacity){
    static const unsigned faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    static const unsigned tri[6]={0,1,2,0,2,3};
    unsigned n=0;
    for(int i=0;i<MC_BREAK_STAGES && n+36<=capacity;i++){
        const McBreakStage *s=&fx->stages[i];if(!s->active)continue;
        float x=(float)(s->x-camera[0]),y=(float)(s->y-camera[1]),z=(float)(s->z-camera[2]);
        uint32_t color=vertex_color(0x101010,.12f+s->stage*.06f);
        for(int f=0;f<6;f++)for(int j=0;j<6;j++){unsigned c=faces[f][tri[j]];
            out[n++]=(McEntityVertex){x+(c&1?1.004f:-.004f),y+(c&2?1.004f:-.004f),z+(c&4?1.004f:-.004f),color};}
    }
    return n;
}
