#ifndef MC_SOUND_H
#define MC_SOUND_H
#include "entities.h"
#define MC_SOUND_VOICES 16
#define MC_SOUND_RATE 48000
typedef struct {char name[128];int active,category,entity;float volume,pitch,range,phase,step;int tonal;double x,y,z;unsigned age,length,noise;} McSoundVoice;
typedef struct {McSoundVoice voices[MC_SOUND_VOICES];unsigned next;uint64_t played,dropped;} McSound;
void mc_sound_init(McSound *s);
void mc_sound_play(McSound *s,const char *name,int category,double x,double y,double z,float volume,float pitch,float range,int entity,uint64_t seed);
int mc_sound_packet(McSound *s,const McEntities *entities,const McClient *c,uint32_t id,const uint8_t *p,size_t n);
/* Caller serializes mutation; signed 16-bit stereo, always fills frames*2 samples. */
void mc_sound_mix(McSound *s,int16_t *pcm,size_t frames,double x,double y,double z,float yaw);
#endif
