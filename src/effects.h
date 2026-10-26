#ifndef MC_EFFECTS_H
#define MC_EFFECTS_H
#include "entities.h"
#include "entity_mesh.h"
/* Client-side particles and block-breaking overlays driven by server packets.
   Particles are untextured billboards with original colour approximations. */
#define MC_PARTICLES 768
#define MC_PARTICLES_PER_PACKET 64
#define MC_BREAK_STAGES 32
typedef struct {const char *name;uint32_t color;float size;int life;float gravity;} McParticleStyle;
typedef struct {double x,y,z;float vx,vy,vz,size,gravity;uint32_t color;uint16_t age,life;} McParticle;
typedef struct {int active,entity,x,y,z,stage;} McBreakStage;
typedef struct {
    McParticle particles[MC_PARTICLES];
    unsigned count;
    McBreakStage stages[MC_BREAK_STAGES];
    uint32_t rng;
    uint64_t last_tick,spawned,replaced,packets;
    char error[64];
} McEffects;
void mc_effects_init(McEffects *fx);
int mc_particle_type(const char *name);
const McParticleStyle *mc_particle_style(int type);
/* Spawn one particle; velocity in blocks per tick. Oldest particles are replaced when full. */
void mc_effects_spawn(McEffects *fx,int type,double x,double y,double z,float vx,float vy,float vz,uint32_t color);
/* Handles particle, explosion, world event, block destroy stage, animation and
   entity event packets. Entities may be NULL. Returns -1 for malformed packets. */
int mc_effects_packet(McEffects *fx,const McEntities *entities,uint32_t id,const uint8_t *p,size_t n,uint64_t now);
/* Advances particle physics in 50 ms ticks (bounded catch-up). */
void mc_effects_update(McEffects *fx,uint64_t now);
/* Camera-relative billboard triangles (6 vertices per particle), nearest first
   is not guaranteed. right/up are the camera's unit axes. */
unsigned mc_effects_particles(const McEffects *fx,const double camera[3],const float right[3],const float up[3],McEntityVertex *out,unsigned capacity);
/* Camera-relative translucent boxes over blocks being broken. */
unsigned mc_effects_cracks(const McEffects *fx,const double camera[3],McEntityVertex *out,unsigned capacity);
#endif
