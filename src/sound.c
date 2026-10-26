#include "sound.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "sound_data.inc"
void mc_sound_init(McSound *s){memset(s,0,sizeof(*s));}
void mc_sound_play(McSound *s,const char *name,int category,double x,double y,double z,float volume,float pitch,float range,int entity,uint64_t seed){
    if(!isfinite(volume) || volume<=0 || !isfinite(pitch) || pitch<=0 || !isfinite(range) || range<=0)return;
    /* Music and looping ambience require an asset player, not an impact cue. */
    if(category==1 || category==2 || strstr(name,"ambient") || strstr(name,"music"))return;
    McSoundVoice *v=NULL;
    for(unsigned i=0;i<MC_SOUND_VOICES;i++){unsigned at=(s->next+i)%MC_SOUND_VOICES;if(!s->voices[at].active){v=&s->voices[at];s->next=(at+1)%MC_SOUND_VOICES;break;}}
    if(!v){s->dropped++;return;}
    memset(v,0,sizeof(*v));snprintf(v->name,sizeof(v->name),"%s",name);v->active=1;v->category=category;v->entity=entity;
    v->x=x;v->y=y;v->z=z;v->volume=fminf(volume,4);v->pitch=fminf(fmaxf(pitch,0.25f),4);v->range=range;
    v->tonal=strstr(name,"experience") || strstr(name,"note") || strstr(name,"ui.");v->step=(v->tonal?880:180)*v->pitch/MC_SOUND_RATE;
    v->noise=(unsigned)seed|1;v->length=strstr(name,"break")?12000:strstr(name,"step")?4000:9000;s->played++;
}
static int boolean(McReader *r){int v=mc_u8(r);if(v>1)r->error=1;return v;}
int mc_sound_packet(McSound *s,const McEntities *entities,const McClient *c,uint32_t id,const uint8_t *p,size_t n){
    if(id==0x28 || id==0x41){mc_sound_init(s);return 0;}
    if(id!=0x61 && id!=0x62 && id!=0x63 && id!=0x25)return 0;
    McReader r={p,n,0,0};char name[128]={0};
    if(id==0x63){
        unsigned flags=mc_u8(&r);if(flags>3)return -1;int category=-1;
        if(flags&1){category=mc_varint_read(&r);if(category<0 || category>9)return -1;}
        if(flags&2)mc_string(&r,name,sizeof(name));
        if(r.error || r.pos!=n)return -1;
        for(int i=0;i<MC_SOUND_VOICES;i++){McSoundVoice *v=&s->voices[i];if((category<0 || v->category==category) && (!(flags&2) || !strcmp(name,v->name)))v->active=0;}
        return 0;
    }
    if(id==0x25){
        unsigned event=mc_u32(&r);uint64_t pos=mc_u64(&r);mc_u32(&r);boolean(&r);
        if(r.error || r.pos!=n)return -1;
        if(event==2001){int x=(int32_t)(pos>>38);if(x&(1<<25))x|=~0x3ffffff;int z=(pos>>12)&0x3ffffff;if(z&(1<<25))z|=~0x3ffffff;int y=pos&4095;if(y&2048)y-=4096;
            mc_sound_play(s,"minecraft:block.stone.break",4,x+.5,y+.5,z+.5,1,1,16,-1,pos);}
        return 0;
    }
    unsigned sound=mc_varint_read(&r);float range=0;
    if(!sound){mc_string(&r,name,sizeof(name));if(boolean(&r)){range=mc_f32(&r);if(!isfinite(range) || range<=0)return -1;}}
    else if(sound<sizeof(sound_names)/sizeof(sound_names[0]))snprintf(name,sizeof(name),"minecraft:%s",sound_names[sound]);
    unsigned category=mc_varint_read(&r);int entity=-1;double x,y,z;
    if(id==0x61){entity=mc_varint_read(&r);const McEntity *e=mc_entity_get(entities,entity);
        x=e?e->x:c->x;y=e?e->y:c->y;z=e?e->z:c->z;if(!e && entity!=c->entity_id)name[0]=0;
    }else{x=(int32_t)mc_u32(&r)/8.0;y=(int32_t)mc_u32(&r)/8.0;z=(int32_t)mc_u32(&r)/8.0;}
    float volume=mc_f32(&r),pitch=mc_f32(&r);uint64_t seed=mc_u64(&r);
    if(r.error || r.pos!=n || category>9 || !isfinite(volume) || volume<0 || !isfinite(pitch) || pitch<=0)return -1;
    if(!range)range=16*fmaxf(1,volume);
    if(name[0])mc_sound_play(s,name,category,x,y,z,volume,pitch,range,entity,seed);
    return 0;
}
void mc_sound_mix(McSound *s,int16_t *pcm,size_t frames,double x,double y,double z,float yaw){
    memset(pcm,0,frames*2*sizeof(*pcm));
    float left[MC_SOUND_VOICES],right[MC_SOUND_VOICES];
    float a=yaw*0.01745329252f;
    for(int k=0;k<MC_SOUND_VOICES;k++){
        McSoundVoice *v=&s->voices[k];double dx=v->x-x,dy=v->y-y,dz=v->z-z,d=sqrt(dx*dx+dy*dy+dz*dz);
        float gain=v->active?fmaxf(0,1-d/v->range)*fminf(v->volume,1)*3500:0;
        float pan=d>0.01?-(dx*cosf(a)+dz*sinf(a))/d:0;
        left[k]=gain*(1-pan)*0.5f;right[k]=gain*(1+pan)*0.5f;
    }
    for(size_t i=0;i<frames;i++){
        float l=0,r=0;
        for(int k=0;k<MC_SOUND_VOICES;k++){
            McSoundVoice *v=&s->voices[k];if(!v->active)continue;
            v->phase+=v->step;if(v->phase>=1)v->phase-=1;
            v->noise=v->noise*1664525u+1013904223u;
            float noise=(int32_t)v->noise/2147483648.0f,tone=1-4*fabsf(v->phase-.5f);
            float env=1-(float)v->age/v->length;float attack=fminf(v->age/96.0f,1);
            float sample=(v->tonal?tone:.75f*noise+.25f*tone)*env*env*attack;
            l+=sample*left[k];r+=sample*right[k];if(++v->age>=v->length)v->active=0;
        }
        pcm[i*2]=(int16_t)fminf(32767,fmaxf(-32768,l));pcm[i*2+1]=(int16_t)fminf(32767,fmaxf(-32768,r));
    }
}
