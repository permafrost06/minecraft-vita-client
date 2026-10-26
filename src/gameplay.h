#ifndef MC_GAMEPLAY_H
#define MC_GAMEPLAY_H
#include "interaction.h"
#include "inventory.h"
#include "entities.h"
#include "game_data.h"
float mc_break_delta(uint16_t state,const McItem *tool,int grounded,int underwater,int aqua_affinity,int haste,int fatigue);
enum { MC_ACTION_DIG,MC_ACTION_USE_BLOCK,MC_ACTION_USE_AIR,MC_ACTION_RELEASE,MC_ACTION_ATTACK,MC_ACTION_INTERACT,MC_ACTION_SWING };
#define MC_GAME_ACTIONS 8
typedef struct {int type,status,hand,entity;McTarget target;} McGameAction;
typedef struct {
    int mining,finished,slot,tool,using_item,using_ack,hand,used_item;
    float progress;
    McTarget target;
    uint64_t next_left,next_right,next_swing,next_attack,finished_at,use_started,using_updates;
} McGameplay;
void mc_gameplay_movement(const McClient *c,const McInventory *inv,McPlayer *p,McInput *input,uint64_t now);
void mc_gameplay_reset(McGameplay *g);
/* Fixed 20Hz tick. Reads world/inventory/entities under caller's lock and emits
   packet intents. The caller sends them after releasing the lock. */
unsigned mc_gameplay_tick(McGameplay *g,const McClient *c,const McWorld *w,const McInventory *inv,const McEntities *entities,const McPlayer *player,int enabled,int left,int right,uint64_t now,McGameAction out[MC_GAME_ACTIONS]);
int mc_gameplay_send(McClient *c,const McGameAction *a);
#endif
