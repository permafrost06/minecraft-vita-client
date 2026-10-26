#include "entity_mesh.h"
#include <math.h>
typedef struct { McEntityVertex *v;unsigned count,capacity;float yaw,scale; } Builder;
static uint32_t rgba(uint32_t rgb,float shade){
    unsigned r=((rgb>>16)&255)*shade,g=((rgb>>8)&255)*shade,b=(rgb&255)*shade;
    return 0xff000000u|(b<<16)|(g<<8)|r;
}
static void box(Builder *b,float x,float y,float z,float w,float h,float d,uint32_t color){
    if(b->count+36>b->capacity)return;
    static const unsigned faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    static const unsigned tri[6]={0,1,2,0,2,3};
    static const float shades[6]={.75f,.85f,.6f,1,.8f,.9f};
    float sy=sinf(b->yaw),cy=cosf(b->yaw);
    for(int f=0;f<6;f++)for(int j=0;j<6;j++){
        unsigned c=faces[f][tri[j]];
        float px=(x+(c&1?w/2:-w/2))*b->scale,py=(y+(c&2?h:0))*b->scale,pz=(z+(c&4?d/2:-d/2))*b->scale;
        b->v[b->count++]=(McEntityVertex){px*cy-pz*sy,py,px*sy+pz*cy,rgba(color,shades[f])};
    }
}
unsigned mc_entity_model(const McEntity *e,uint64_t now,McEntityVertex *out,unsigned capacity){
    if(!e->active || (e->flags&0x20) || capacity<MC_ENTITY_MODEL_VERTICES)return 0;
    const McEntityDef *d=mc_entity_def(e->type);
    Builder b={out,0,capacity,e->yaw*.0174532925199433f,e->baby?.5f:1};
    float w=d->width,h=d->height;uint32_t color=d->color;
    if(e->type==MC_ENTITY_PLAYER){color=0x4098b8;}
    if(d->kind==3 || d->kind==4){
        b.yaw=now*.002f;float lift=.12f+.05f*sinf(now*.004f);
        color=d->kind==4?0x98ee40:0xf0d888;
        if(d->kind==3 && !e->item.count)return 0;
        box(&b,0,lift,0,.22f,.22f,.22f,color);
    }else if(d->kind==1){
        if(e->pose==5 || e->pose==3 || e->pose==1)h*=.45f;
        else if(e->pose==2)h*=.7f;
        float head=h*.25f,leg=h*.38f,body=h-head-leg;
        box(&b,0,leg,0,w*.68f,body,w*.42f,color);
        float turn=b.yaw;b.yaw=e->head_yaw*.0174532925199433f;
        box(&b,0,h-head,0,head,head,head,e->type==MC_ENTITY_PLAYER?0xd5a47e:color);b.yaw=turn;
        float swing=(fabsf(e->velocity[0])+fabsf(e->velocity[2])>.001f || fabs(e->x-e->from[0])+fabs(e->z-e->from[2])>.002)?sinf(now*.012f)*.06f:0;
        box(&b,-w*.18f,0,swing,w*.3f,leg,w*.35f,0x405780);
        box(&b,w*.18f,0,-swing,w*.3f,leg,w*.35f,0x405780);
        box(&b,-w*.48f,leg,0,w*.24f,body,w*.3f,color);
        box(&b,w*.48f,leg,0,w*.24f,body,w*.3f,color);
        if(e->equipment[0].count)box(&b,-w*.55f,leg,-w*.3f,.12f,.35f,.12f,0xc0b090);
        if(e->equipment[5].count)box(&b,0,h-head*.9f,0,head*1.08f,head*.8f,head*1.08f,0xb8bcc0);
        if(e->equipment[4].count)box(&b,0,leg,0,w*.75f,body*.95f,w*.48f,0xb8bcc0);
    }else if(d->kind==2){
        float leg=h*.32f;
        box(&b,0,leg,0,w*.85f,h*.48f,w*1.25f,color);
        box(&b,0,h*.53f,-w*.65f,w*.6f,h*.4f,w*.55f,color);
        for(int i=0;i<4;i++)box(&b,(i&1?1:-1)*w*.28f,0,(i&2?1:-1)*w*.4f,w*.2f,leg,w*.22f,color);
    }else box(&b,0,0,0,w,h,w,color);
    return b.count;
}
