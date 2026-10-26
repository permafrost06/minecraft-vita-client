#include "entities.h"
#include "nbt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "entity_data.inc"
static const McEntityDef unknown={-1,"Entity",.6f,1.0f,0,-1,-1,-1,0xa09070};
const McEntityDef *mc_entity_def(int type){
    return type>=0 && (unsigned)type<sizeof(entity_defs)/sizeof(entity_defs[0]) && entity_defs[type].type==type?&entity_defs[type]:&unknown;
}
void mc_entities_init(McEntities *s){memset(s,0,sizeof(*s));s->local_id=-1;}
static McEntity *find(McEntities *s,int id){for(int i=0;i<MC_MAX_ENTITIES;i++)if(s->entries[i].active && s->entries[i].id==id)return &s->entries[i];return NULL;}
const McEntity *mc_entity_get(const McEntities *s,int id){for(int i=0;i<MC_MAX_ENTITIES;i++)if(s->entries[i].active && s->entries[i].id==id)return &s->entries[i];return NULL;}
const char *mc_entity_name(const McEntities *s,const McEntity *e){
    if(e->type==MC_ENTITY_PLAYER)for(int i=0;i<MC_MAX_PROFILES;i++)if(s->profiles[i].active && !memcmp(s->profiles[i].uuid,e->uuid,16))return s->profiles[i].name;
    return mc_entity_def(e->type)->name;
}
void mc_entity_position(const McEntity *e,uint64_t now,double p[3]){
    double t=now>e->moved_at?(now-e->moved_at)/50.0:0;if(t>1)t=1;
    p[0]=e->from[0]+(e->x-e->from[0])*t;p[1]=e->from[1]+(e->y-e->from[1])*t;p[2]=e->from[2]+(e->z-e->from[2])*t;
}
static int bad(McEntities *s){snprintf(s->error,sizeof(s->error),"Malformed entity packet");return -1;}
static unsigned boolean(McReader *r){unsigned b=mc_u8(r);if(b>1)r->error=1;return b;}
static void string_skip(McReader *r){uint32_t n=mc_varint_read(r);mc_skip(r,n);}
static void blob_skip(McReader *r){uint32_t n=mc_varint_read(r);mc_skip(r,n);}
static int entity_id(McReader *r){uint32_t id=mc_varint_read(r);if(id>INT32_MAX)r->error=1;return (int)id;}
static float angle(McReader *r){return mc_u8(r)*(360.0f/256);}
static int valid_position(const McEntity *e){return isfinite(e->x) && isfinite(e->y) && isfinite(e->z) && fabs(e->x)<=30000000 && fabs(e->y)<=30000000 && fabs(e->z)<=30000000;}
static void floats(McReader *r,int n){for(int i=0;i<n;i++)if(!isfinite(mc_f32(r)))r->error=1;}
static void particle(McReader *r){
    unsigned type=mc_varint_read(r);
    if(type==2 || type==3 || type==25 || type==95)mc_varint_read(r);
    else if(type==14)floats(r,4);
    else if(type==15)floats(r,7);
    else if(type==33)floats(r,1);
    else if(type==42)mc_read_slot(r);
    else if(type==43){
        char position[32];mc_string(r,position,sizeof(position));
        if(!strcmp(position,"minecraft:block"))mc_skip(r,8);
        else if(!strcmp(position,"minecraft:entity")){mc_varint_read(r);floats(r,1);}
        else r->error=1;
        mc_varint_read(r);
    }
}
int mc_entity_read_metadata(McReader *r,McEntity *e){
    const McEntityDef *d=mc_entity_def(e->type);
    for(unsigned n=0;n<256 && !r->error;n++){
        unsigned key=mc_u8(r);if(key==255)return r->error?-1:0;
        unsigned type=mc_varint_read(r);uint32_t value=0;float real=0;McSlot slot={-1,0};
        switch(type){
        case 0:value=mc_u8(r);break;
        case 1:case 12:case 14:case 15:case 19:case 20:case 21:case 22:case 24:case 25:value=mc_varint_read(r);break;
        case 2:mc_varlong(r);break;
        case 3:real=mc_f32(r);if(!isfinite(real))r->error=1;break;
        case 4:case 5:string_skip(r);break;
        case 6:if(boolean(r))string_skip(r);break;
        case 7:slot=mc_read_slot(r);break;
        case 8:value=boolean(r);break;
        case 9:case 26:floats(r,3);break;
        case 10:mc_skip(r,8);break;
        case 11:if(boolean(r))mc_skip(r,8);break;
        case 13:if(boolean(r))mc_skip(r,16);break;
        case 16:mc_nbt_root(r,NULL);break;
        case 17:particle(r);break;
        case 18:mc_varint_read(r);mc_varint_read(r);mc_varint_read(r);break;
        case 23:if(boolean(r)){string_skip(r);mc_skip(r,8);}break;
        case 27:floats(r,4);break;
        default:r->error=1;break;
        }
        if(key==0 && type==0)e->flags=value;
        if(key==1 && type==1)e->air=(int32_t)value;
        if(key==8 && type==0 && d->health_key>=0){e->using_item=!!(value&1);e->using_offhand=!!(value&2);}
        if(key==6 && type==20)e->pose=value;
        if((int)key==d->baby_key && type==8)e->baby=value;
        if((int)key==d->health_key && type==3){e->health=real;e->health_known=1;}
        if((int)key==d->item_key && type==7)e->item=slot;
    }
    r->error=1;return -1;
}
static int profiles(McEntities *s,McReader *r,int remove){
    McProfile updated[MC_MAX_PROFILES];memcpy(updated,s->profiles,sizeof(updated));
    unsigned flags=remove?0:mc_u8(r);uint32_t count=mc_varint_read(r);
    if((flags&~63u) || count>4096)return -1;
    for(uint32_t n=0;n<count && !r->error;n++){
        uint8_t uuid[16];if(mc_skip(r,16)<0)break;memcpy(uuid,r->p+r->pos-16,16);
        int index=-1,free_slot=-1;
        for(int i=0;i<MC_MAX_PROFILES;i++){if(updated[i].active && !memcmp(updated[i].uuid,uuid,16))index=i;if(!updated[i].active && free_slot<0)free_slot=i;}
        if(remove){if(index>=0)updated[index].active=0;continue;}
        McProfile p={0};if(index>=0)p=updated[index];else if(flags&1)index=free_slot;
        memcpy(p.uuid,uuid,16);
        if(flags&1){
            mc_string(r,p.name,sizeof(p.name));p.active=1;
            for(int i=0;p.name[i];i++)if((unsigned char)p.name[i]<32)p.name[i]='?';
            uint32_t props=mc_varint_read(r);if(props>1024)return -1;
            for(uint32_t i=0;i<props && !r->error;i++){string_skip(r);string_skip(r);if(boolean(r))string_skip(r);}
        }
        if((flags&2) && boolean(r)){mc_skip(r,24);blob_skip(r);blob_skip(r);}
        if(flags&4)p.gamemode=mc_varint_read(r);
        if(flags&8){p.listed=mc_varint_read(r);if(p.listed>1)r->error=1;}
        if(flags&16)p.latency=mc_varint_read(r);
        if((flags&32) && boolean(r))string_skip(r);
        if(index>=0)updated[index]=p;
    }
    if(r->error || r->pos!=r->n)return -1;
    memcpy(s->profiles,updated,sizeof(updated));return 0;
}
int mc_entities_packet(McEntities *s,uint32_t id,const uint8_t *p,size_t n,uint64_t now){
    McReader r={p,n,0,0};
    if(id==0x28){if(n<4)return bad(s);mc_entities_init(s);s->local_id=(int32_t)mc_u32(&r);return 0;}
    if(id==0x41){memset(s->entries,0,sizeof(s->entries));s->count=0;return 0;}
    if(id==0x3a || id==0x39)return profiles(s,&r,id==0x39)<0?bad(s):0;
    if(id==0x3e){
        uint32_t count=mc_varint_read(&r);if(count>4096)return bad(s);
        McReader check=r;for(uint32_t i=0;i<count && !check.error;i++)entity_id(&check);
        if(check.error || check.pos!=n)return bad(s);
        for(uint32_t i=0;i<count;i++){McEntity *e=find(s,entity_id(&r));if(e){e->active=0;s->count--;s->removed++;}}
        return 0;
    }
    int spawn=id==1 || id==2 || id==3;
    if(!spawn && id!=0x2b && id!=0x2c && id!=0x2d && id!=0x42 && id!=0x52 && id!=0x54 && id!=0x55 && id!=0x68 && id!=0x67)return 0;
    int key=entity_id(&r);McEntity *old=find(s,key),e={.id=key,.item={-1,0}};
    if(old)e=*old;
    if(spawn){
        memset(&e,0,sizeof(e));e.id=key;e.active=1;e.item=(McSlot){-1,0};
        for(int i=0;i<6;i++)e.equipment[i]=(McSlot){-1,0};
        if(id!=2){if(mc_skip(&r,16)==0)memcpy(e.uuid,p+r.pos-16,16);}
        e.type=id==1?(int)mc_varint_read(&r):id==3?MC_ENTITY_PLAYER:MC_ENTITY_ORB;
        e.x=mc_f64(&r);e.y=mc_f64(&r);e.z=mc_f64(&r);
        if(id==1){e.pitch=angle(&r);e.yaw=angle(&r);e.head_yaw=angle(&r);mc_varint_read(&r);for(int i=0;i<3;i++)e.velocity[i]=(int16_t)mc_u16(&r)/8000.0f;}
        else if(id==3){e.yaw=angle(&r);e.pitch=angle(&r);e.head_yaw=e.yaw;}
        else mc_u16(&r);
        e.from[0]=e.x;e.from[1]=e.y;e.from[2]=e.z;e.moved_at=now;
        if(e.type<0 || e.type>32767)r.error=1;
    }else if(id==0x2b || id==0x2c || id==0x68){
        mc_entity_position(&e,now,e.from);e.moved_at=now;
        if(id==0x68){e.x=mc_f64(&r);e.y=mc_f64(&r);e.z=mc_f64(&r);}
        else{
            int dx=(int16_t)mc_u16(&r),dy=(int16_t)mc_u16(&r),dz=(int16_t)mc_u16(&r);
            if(dx)e.x=(floor(e.x*4096)+dx)/4096;
            if(dy)e.y=(floor(e.y*4096)+dy)/4096;
            if(dz)e.z=(floor(e.z*4096)+dz)/4096;
        }
        if(id!=0x2b){e.yaw=angle(&r);e.pitch=angle(&r);}
        e.grounded=boolean(&r);
        if(fabs(e.x-e.from[0])+fabs(e.y-e.from[1])+fabs(e.z-e.from[2])>8){e.from[0]=e.x;e.from[1]=e.y;e.from[2]=e.z;}
    }else if(id==0x2d){e.yaw=angle(&r);e.pitch=angle(&r);e.grounded=boolean(&r);}
    else if(id==0x42)e.head_yaw=angle(&r);
    else if(id==0x54){for(int i=0;i<3;i++)e.velocity[i]=(int16_t)mc_u16(&r)/8000.0f;}
    else if(id==0x52)mc_entity_read_metadata(&r,&e);
    else if(id==0x55){
        unsigned slot=128,count=0;
        while((slot&128) && !r.error){slot=mc_u8(&r);McSlot item=mc_read_slot(&r);if((slot&127)>5 || ++count>6)r.error=1;else e.equipment[slot&127]=item;}
    }else if(id==0x67){entity_id(&r);mc_varint_read(&r);}
    if(r.error || r.pos!=n || !valid_position(&e))return bad(s);
    if(id==0x67)return 0; /* Destroy packet owns lifetime; partial pickups may leave a stack. */
    if(spawn){
        if(e.id==s->local_id)return 0;
        if(!old)for(int i=0;i<MC_MAX_ENTITIES;i++)if(!s->entries[i].active){old=&s->entries[i];s->count++;break;}
        if(!old){s->overflow++;return 0;}
        s->spawned++;
    }
    if(old){*old=e;if(id==0x52)s->metadata++;else if(id==0x55)s->equipment++;else if(!spawn)s->moved++;}
    return 0;
}
