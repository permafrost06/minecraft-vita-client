#ifndef MC_ENTITIES_H
#define MC_ENTITIES_H
#include "reader.h"
#define MC_MAX_ENTITIES 512
#define MC_MAX_PROFILES 128
#define MC_ENTITY_PLAYER 122
#define MC_ENTITY_ITEM 54
#define MC_ENTITY_ORB 34
/* kind: generic, humanoid, quadruped, item, orb. Metadata indices vary by type. */
typedef struct { int type; const char *name; float width,height; int kind,baby_key,item_key,health_key; uint32_t color; } McEntityDef;
typedef struct {
    int active,id,type,flags,pose,baby,grounded,air,using_item,using_offhand,health_known;
    int bed[3],has_bed,vehicle;
    uint8_t uuid[16];
    double x,y,z,from[3];
    float yaw,pitch,head_yaw,velocity[3],health;
    uint64_t moved_at,hurt_at,swing_at,dead_at;
    int swing_offhand;
    McSlot item,equipment[6];
} McEntity;
typedef struct { int active; uint8_t uuid[16]; char name[17]; int gamemode,listed,latency; } McProfile;
typedef struct {
    McEntity entries[MC_MAX_ENTITIES];
    McProfile profiles[MC_MAX_PROFILES];
    int count,local_id,local_vehicle,local_driver,local_op_level;
    uint64_t local_hurt_at,local_totem_at,local_hurts;
    uint64_t spawned,removed,moved,metadata,equipment,overflow;
    char error[128];
} McEntities;
void mc_entities_init(McEntities *s);
/* Caller serializes access. Failed packets never partially mutate an entity. */
int mc_entities_packet(McEntities *s,uint32_t id,const uint8_t *p,size_t n,uint64_t now);
const McEntity *mc_entity_get(const McEntities *s,int id);
const McEntityDef *mc_entity_def(int type);
const char *mc_entity_name(const McEntities *s,const McEntity *e);
int mc_entity_read_metadata(McReader *r,McEntity *e);
void mc_entity_position(const McEntity *e,uint64_t now,double pos[3]);
#endif
