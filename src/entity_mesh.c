#include "entity_mesh.h"
#include <math.h>
typedef struct { McEntityVertex *v;unsigned count,capacity;float yaw,scale,roll,hurt; } Builder;
static uint32_t rgba(uint32_t rgb,float shade,float hurt){
    float tint=hurt*.6f;
    unsigned r=(((rgb>>16)&255)*(1-tint)+255*tint)*shade,g=(((rgb>>8)&255)*(1-tint)+40*tint)*shade,b=((rgb&255)*(1-tint)+40*tint)*shade;
    return 0xff000000u|(b<<16)|(g<<8)|r;
}
static void box(Builder *b,float x,float y,float z,float w,float h,float d,uint32_t color){
    if(b->count+36>b->capacity)return;
    static const unsigned faces[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    static const unsigned tri[6]={0,1,2,0,2,3};
    static const float shades[6]={.75f,.85f,.6f,1,.8f,.9f};
    float sy=sinf(b->yaw),cy=cosf(b->yaw),sr=sinf(b->roll),cr=cosf(b->roll);
    for(int f=0;f<6;f++)for(int j=0;j<6;j++){
        unsigned c=faces[f][tri[j]];
        float px=(x+(c&1?w/2:-w/2))*b->scale,py=(y+(c&2?h:0))*b->scale,pz=(z+(c&4?d/2:-d/2))*b->scale;
        /* Death animation: tip over sideways around the feet. */
        float rx=px*cr-py*sr,ry=px*sr+py*cr;
        b->v[b->count++]=(McEntityVertex){rx*cy-pz*sy,ry,rx*sy+pz*cy,rgba(color,shades[f],b->hurt)};
    }
}
unsigned mc_entity_model(const McEntity *e,uint64_t now,McEntityVertex *out,unsigned capacity){
    if(!e->active || (e->flags&0x20) || capacity<MC_ENTITY_MODEL_VERTICES)return 0;
    const McEntityDef *d=mc_entity_def(e->type);
    Builder b={out,0,capacity,e->yaw*.0174532925199433f,e->baby?.5f:1,0,0};
    if(e->hurt_at && now>=e->hurt_at && now-e->hurt_at<500)b.hurt=1-(now-e->hurt_at)/500.0f;
    if(e->dead_at && now>=e->dead_at){float t=(now-e->dead_at)/1000.0f;b.roll=sqrtf(t>1?1:t)*1.5707963f;b.hurt=1;}
    float w=d->width,h=d->height;uint32_t color=d->color;
    if(e->type==MC_ENTITY_PLAYER){color=0x4098b8;}
    if(e->type==9 || e->type==13 || e->type==64 || e->type==14 || e->type==17 || e->type==40 || e->type==48 || e->type==93 || e->type==102){
        int boat=e->type==9 || e->type==13;float width=boat?1.375f:.98f,length=boat?1.8f:.98f,height=boat?.56f:.7f,wall=.12f;
        color=boat?0x987044:0x92989c;
        box(&b,0,0,0,width,.12f,length,color);
        box(&b,-width/2+wall/2,.12f,0,wall,height-.12f,length,color);
        box(&b,width/2-wall/2,.12f,0,wall,height-.12f,length,color);
        box(&b,0,.12f,-length/2+wall/2,width-wall*2,height-.12f,wall,color);
        box(&b,0,.12f,length/2-wall/2,width-wall*2,height-.12f,wall,color);
        if(e->type==13 || e->type==14)box(&b,0,.12f,0,.65f,.55f,.65f,0xa07842);
        else if(boat){box(&b,0,.18f,-.35f,width-wall*2,.1f,.2f,color);box(&b,0,.18f,.35f,width-wall*2,.1f,.2f,color);}
    }else if(d->kind==3 || d->kind==4){
        b.yaw=now*.002f;float lift=.12f+.05f*sinf(now*.004f);
        color=d->kind==4?0x98ee40:0xf0d888;
        if(d->kind==3 && !e->item.count)return 0;
        box(&b,0,lift,0,.22f,.22f,.22f,color);
    }else if(d->kind==1){
        if(e->pose==2 || e->pose==3 || e->pose==1)h*=.45f;
        else if(e->pose==5)h*=1.5f/1.8f;
        float head=h*.25f,leg=h*.38f,body=h-head-leg;
        box(&b,0,leg,0,w*.68f,body,w*.42f,color);
        float turn=b.yaw;b.yaw=e->head_yaw*.0174532925199433f;
        box(&b,0,h-head,0,head,head,head,e->type==MC_ENTITY_PLAYER?0xd5a47e:color);b.yaw=turn;
        float swing=(fabsf(e->velocity[0])+fabsf(e->velocity[2])>.001f || fabs(e->x-e->from[0])+fabs(e->z-e->from[2])>.002)?sinf(now*.012f)*.06f:0;
        box(&b,-w*.18f,0,swing,w*.3f,leg,w*.35f,0x405780);
        box(&b,w*.18f,0,-swing,w*.3f,leg,w*.35f,0x405780);
        /* Arm swing from the server's animation packet; main hand is the left box. */
        float arm[2]={0,0};
        if(e->swing_at && now>=e->swing_at && now-e->swing_at<300)arm[e->swing_offhand]=sinf((now-e->swing_at)/300.0f*3.14159265f);
        box(&b,-w*.48f,leg+body*.3f*arm[0],-w*.55f*arm[0],w*.24f,body,w*.3f,color);
        box(&b,w*.48f,leg+body*.3f*arm[1],-w*.55f*arm[1],w*.24f,body,w*.3f,color);
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
