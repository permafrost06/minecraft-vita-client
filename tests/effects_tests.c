#include "effects.h"
#include "blocks.h"
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
typedef struct {uint8_t p[512];size_t n;} Buf;
static void u8(Buf *b,unsigned v){b->p[b->n++]=(uint8_t)v;}
static void vi(Buf *b,uint32_t v){b->n+=mc_varint_write(b->p+b->n,v);}
static void u32(Buf *b,uint32_t v){for(int i=3;i>=0;i--)u8(b,(v>>(i*8))&255);}
static void u64(Buf *b,uint64_t v){for(int i=7;i>=0;i--)u8(b,(v>>(i*8))&255);}
static void f32(Buf *b,float v){uint32_t bits;memcpy(&bits,&v,4);u32(b,bits);}
static void f64(Buf *b,double v){uint64_t bits;memcpy(&bits,&v,8);u64(b,bits);}
static uint64_t pos(int x,int y,int z){return ((uint64_t)(x&0x3ffffff)<<38)|((uint64_t)(z&0x3ffffff)<<12)|(uint64_t)(y&0xfff);}
static void particle(Buf *b,int type,double x,float offset,float speed,int count){
    b->n=0;vi(b,type);u8(b,0);f64(b,x);f64(b,64);f64(b,-3);f32(b,offset);f32(b,offset);f32(b,offset);f32(b,speed);u32(b,count);
}
int main(void){
    mc_mesh_init();static McEffects fx;mc_effects_init(&fx);
    CHECK(mc_particle_type("dust")==14 && mc_particle_type("block")==2 && mc_particle_type("shriek")==95);
    /* Dust carries its own colour; count 0 means one particle with offset*speed velocity. */
    Buf b;particle(&b,14,1.5,.5f,.2f,0);f32(&b,0);f32(&b,1);f32(&b,0);f32(&b,1);
    CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)==0 && fx.count==1);
    CHECK((fx.particles[0].color&0xffffff)==0x00ff00 && fx.particles[0].vx>.099f && fx.particles[0].vx<.101f);
    /* Gaussian spread, capped per packet. */
    particle(&b,53,0,.5f,.01f,1000);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)==0 && fx.count==1+MC_PARTICLES_PER_PACKET);
    /* Block particles use the block colour; air spawns nothing. */
    mc_effects_init(&fx);particle(&b,2,0,0,0,3);vi(&b,1);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)==0 && fx.count==3);
    CHECK((fx.particles[0].color&0xffffff)==mc_block_color(1) && mc_block_color(1) && !mc_block_color(0));
    particle(&b,2,0,0,0,3);vi(&b,0);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)==0 && fx.count==3);
    /* Unknown types and truncated data are rejected; item data is ignored. */
    particle(&b,96,0,0,0,1);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)<0);
    particle(&b,14,0,0,0,1);f32(&b,1);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)<0);
    particle(&b,42,0,0,0,1);u8(&b,1);vi(&b,5);u8(&b,1);u8(&b,0);CHECK(mc_effects_packet(&fx,NULL,0x26,b.p,b.n,0)==0);
    /* Block break world event: 27 fragments around the block. */
    mc_effects_init(&fx);b.n=0;u32(&b,2001);u64(&b,pos(-4,-60,7));u32(&b,1);u8(&b,0);
    CHECK(mc_effects_packet(&fx,NULL,0x25,b.p,b.n,0)==0 && fx.count==27);
    CHECK(fx.particles[0].x>-4 && fx.particles[0].x<-3 && fx.particles[0].y>-60 && fx.particles[0].y<-59 && fx.particles[0].z>7 && fx.particles[0].z<8);
    /* Explosion: emitter burst plus smoke per destroyed block. */
    mc_effects_init(&fx);b.n=0;f64(&b,.5);f64(&b,64);f64(&b,.5);f32(&b,4);vi(&b,2);u8(&b,1);u8(&b,0);u8(&b,0);u8(&b,0xff);u8(&b,0);u8(&b,0);f32(&b,0);f32(&b,0);f32(&b,0);
    CHECK(mc_effects_packet(&fx,NULL,0x1d,b.p,b.n,0)==0 && fx.count==12);
    b.n=0;f64(&b,.5);f64(&b,64);f64(&b,.5);f32(&b,4);vi(&b,9);CHECK(mc_effects_packet(&fx,NULL,0x1d,b.p,b.n,0)<0);
    /* Particles age out on 50 ms ticks. */
    mc_effects_update(&fx,1000);CHECK(fx.count==12);for(int i=1;i<=40;i++)mc_effects_update(&fx,1000+i*100);CHECK(fx.count==0);
    /* Destroy stages: add, update, remove with stage 10. */
    b.n=0;vi(&b,7);u64(&b,pos(1,2,3));u8(&b,4);CHECK(mc_effects_packet(&fx,NULL,0x07,b.p,b.n,0)==0 && fx.stages[0].active && fx.stages[0].stage==4 && fx.stages[0].z==3);
    b.n=0;vi(&b,7);u64(&b,pos(1,2,3));u8(&b,9);CHECK(mc_effects_packet(&fx,NULL,0x07,b.p,b.n,0)==0 && fx.stages[0].stage==9 && !fx.stages[1].active);
    McEntityVertex v[64];double camera[3]={0,0,0};CHECK(mc_effects_cracks(&fx,camera,v,64)==36);
    b.n=0;vi(&b,7);u64(&b,pos(1,2,3));u8(&b,10);CHECK(mc_effects_packet(&fx,NULL,0x07,b.p,b.n,0)==0 && !fx.stages[0].active);
    /* Billboards: 6 vertices per particle, camera relative. */
    mc_effects_spawn(&fx,mc_particle_type("flame"),10,0,0,0,0,0,0);
    float right[3]={1,0,0},up[3]={0,1,0};CHECK(mc_effects_particles(&fx,camera,right,up,v,64)==6 && v[0].x>9.9f && v[0].x<10);
    /* A full pool replaces the oldest particle. */
    for(int i=0;i<MC_PARTICLES+5;i++)mc_effects_spawn(&fx,mc_particle_type("smoke"),0,0,0,0,0,0,0);
    CHECK(fx.count==MC_PARTICLES && fx.replaced==6);
    puts("Effects checks passed: particle data/spread/caps, block colours, block break, explosion, ageing, destroy stages, billboards");
    return 0;
}
