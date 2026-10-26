#include "gameplay.h"
#include "blocks.h"
#include "items.h"
#include <math.h>
#include <string.h>
float mc_break_delta(uint16_t state,const McItem *tool,int grounded,int underwater,int aqua_affinity,int haste,int fatigue){
    const McDigDef *d=mc_dig_def(state);if(!d->diggable || d->hardness<0)return 0;if(d->hardness==0)return 1;
    int harvest;float speed=mc_block_tool_speed(state,tool?tool->stack.item:-1,&harvest);
    if(speed>1){int efficiency=mc_item_enchantment(tool,"efficiency");if(efficiency)speed+=efficiency*efficiency+1;}
    if(haste>=0)speed*=1+.2f*(haste+1);
    if(fatigue>=0){static const float factors[4]={.3f,.09f,.0027f,.00081f};speed*=factors[fatigue>3?3:fatigue];}
    if(underwater && !aqua_affinity)speed/=5;
    if(!grounded)speed/=5;
    return speed/d->hardness/(harvest?30:100);
}
static int effect_level(const McClient *c,int effect,uint64_t now){return c->effect_until[effect]>now?c->effect_amplifier[effect]+1:0;}
void mc_gameplay_movement(McClient *c,const McInventory *inv,McPlayer *p,McInput *input,uint64_t now){
    p->sleeping=c->pose==2;
    if(p->sleeping && c->has_bed){p->x=c->bed[0]+.5;p->y=c->bed[1]+.6875;p->z=c->bed[2]+.5;}
    if(p->sleeping && input->jump && !p->jump_held)mc_player_command(c,2);
    int elytra=inv->player[6].stack.count && !strcmp(mc_item_def(inv->player[6].stack.item)->name,"elytra");
    p->gliding=!!(c->entity_flags&0x80) && elytra && !c->flying && !p->sleeping && !p->grounded;
    if(!p->sleeping && !p->gliding && elytra && input->jump && !p->jump_held && !p->grounded && !p->in_water && !p->in_lava && !c->flying && !effect_level(c,25,now))mc_player_command(c,8);
    p->jump_held=!!input->jump;
    p->flying=c->flying;p->speed_scale=(float)(c->movement_speed/.1);
    p->jump_boost=effect_level(c,8,now);p->levitation=effect_level(c,25,now);p->slow_falling=effect_level(c,28,now);p->dolphins_grace=effect_level(c,30,now);
    p->depth_strider=mc_item_enchantment(&inv->player[8],"depth_strider");p->soul_speed=mc_item_enchantment(&inv->player[8],"soul_speed");
    p->leather_boots=!strcmp(mc_item_def(inv->player[8].stack.item)->name,"leather_boots");
    if(c->food<=6 && c->gamemode!=1)input->sprint=0;
    if(c->using_item){input->forward*=.2f;input->right*=.2f;input->sprint=0;}
}
void mc_gameplay_reset(McGameplay *g){memset(g,0,sizeof(*g));g->slot=-1;g->tool=-1;g->used_item=-1;}
static void action(McGameAction *out,unsigned *n,int type,int status,int hand,int entity,const McTarget *t){
    if(*n>=MC_GAME_ACTIONS)return;
    McGameAction *a=&out[(*n)++];memset(a,0,sizeof(*a));a->type=type;a->status=status;a->hand=hand;a->entity=entity;if(t)a->target=*t;
}
static int same(const McTarget *a,const McTarget *b){return a->x==b->x && a->y==b->y && a->z==b->z && a->state==b->state;}
static void cancel(McGameplay *g,McGameAction *out,unsigned *n){if(g->mining && !g->finished)action(out,n,MC_ACTION_DIG,1,0,0,&g->target);g->mining=0;g->finished=0;g->progress=0;}
static int entity_target(const McEntities *s,const McClient *c,double limit){
    double rad=.017453292519943295,d[3]={-sin(c->yaw*rad)*cos(c->pitch*rad),-sin(c->pitch*rad),cos(c->yaw*rad)*cos(c->pitch*rad)},o[3]={c->x,c->y+c->eye_height,c->z};
    int best=-1;
    for(int i=0;i<MC_MAX_ENTITIES;i++){
        const McEntity *e=&s->entries[i];if(!e->active || e->id==c->entity_id || e->type==MC_ENTITY_ITEM || e->type==MC_ENTITY_ORB)continue;
        const McEntityDef *def=mc_entity_def(e->type);if(e->health_known && e->health<=0)continue;
        if(def->width<=0 || def->height<=0 || e->type==1)continue;
        double h=def->height*(e->baby?.5:1),half=def->width*(e->baby?.25:.5)+.1;
        if(e->pose==2 || e->pose==3 || e->pose==1)h*=.45;
        else if(e->pose==5)h*=1.5/1.8;
        double lo[3]={e->x-half,e->y-.1,e->z-half},hi[3]={e->x+half,e->y+h+.1,e->z+half},near=0,far=limit;
        for(int a=0;a<3;a++){
            if(fabs(d[a])<1e-12){if(o[a]<lo[a] || o[a]>hi[a]){far=-1;break;}continue;}
            double a0=(lo[a]-o[a])/d[a],a1=(hi[a]-o[a])/d[a];if(a0>a1){double tmp=a0;a0=a1;a1=tmp;}
            if(a0>near)near=a0;
            if(a1<far)far=a1;
        }
        if(near<=far && near<limit){limit=near;best=e->id;}
    }
    return best;
}
unsigned mc_gameplay_tick(McGameplay *g,const McClient *c,const McWorld *w,const McInventory *inv,const McEntities *entities,const McPlayer *p,int enabled,int left,int right,uint64_t now,McGameAction out[MC_GAME_ACTIONS]){
    unsigned n=0;int alive=!c->health_ready || c->health>0;
    enabled=enabled && c->play && c->joined && c->position_ready && c->gamemode!=3 && alive;
    const McItem *held=&inv->player[36+c->selected_slot];
    if(!alive || !c->position_ready || c->pose==2){mc_gameplay_reset(g);return 0;}
    if(!enabled){cancel(g,out,&n);if(g->using_item)action(out,&n,MC_ACTION_RELEASE,0,0,0,NULL);g->using_item=0;return n;}
    if(g->using_item && g->using_updates!=c->using_updates){
        if(c->using_item)g->using_ack=1;
        else if(g->using_ack){g->using_item=0;g->next_right=now+200;}
    }
    if(g->using_item && mc_use_def(g->used_item)->ticks<72000 && now-g->use_started>=(uint64_t)mc_use_def(g->used_item)->ticks*50){g->using_item=0;g->next_right=now+200;}
    g->using_updates=c->using_updates;
    int current_use_item=g->hand?inv->player[45].stack.item:held->stack.item;
    if(g->using_item && (!right || left || current_use_item!=g->used_item)){
        action(out,&n,MC_ACTION_RELEASE,0,0,0,NULL);g->using_item=0;g->next_right=now+200;
    }
    McTarget target;int targeted=mc_target(w,c->x,c->y+c->eye_height,c->z,c->yaw,c->pitch,&target);
    double reach=c->gamemode==1?5:4.5;if(targeted && target.distance>reach)targeted=0;
    int victim=entity_target(entities,c,fmin(targeted?target.distance:reach,left?3:reach));
    if(g->mining && (!left || !targeted || victim>=0 || !same(&target,&g->target) || g->slot!=c->selected_slot || g->tool!=held->stack.item))cancel(g,out,&n);
    if(left){
        if(victim>=0){
            if(now>=g->next_attack){action(out,&n,MC_ACTION_ATTACK,0,0,victim,NULL);action(out,&n,MC_ACTION_SWING,0,0,0,NULL);double speed=c->attack_speed>.05?c->attack_speed:4;g->next_attack=now+(uint64_t)(1000/speed);}
        }else if(targeted){
            if(c->gamemode==1){
                if(now>=g->next_left){action(out,&n,MC_ACTION_DIG,0,0,0,&target);action(out,&n,MC_ACTION_SWING,0,0,0,NULL);g->next_left=now+200;}
            }else if(c->gamemode!=2 || mc_item_can_destroy(held,mc_dig_def(target.state)->name)){
                int started=0;
                if(!g->mining && now>=g->next_left){g->target=target;g->slot=c->selected_slot;g->tool=held->stack.item;g->progress=0;g->mining=1;g->finished=0;started=1;action(out,&n,MC_ACTION_DIG,0,0,0,&target);}
                if(g->mining && !g->finished){
                    uint16_t eye=mc_world_get(w,(int)floor(c->x),(int)floor(c->y+c->eye_height),(int)floor(c->z));
                    int haste=c->effect_until[3]>now?c->effect_amplifier[3]:-1,fatigue=c->effect_until[4]>now?c->effect_amplifier[4]:-1;
                    g->progress+=mc_break_delta(target.state,held,p->grounded,mc_block_fluid(eye)==1,mc_item_enchantment(&inv->player[5],"aqua_affinity")>0,haste,fatigue);
                    if(now>=g->next_swing){action(out,&n,MC_ACTION_SWING,0,0,0,NULL);g->next_swing=now+200;}
                    if(g->progress>=1){if(!started)action(out,&n,MC_ACTION_DIG,2,0,0,&target);g->progress=1;g->finished=1;g->finished_at=now;g->next_left=now+250;}
                }else if(g->finished && now-g->finished_at>=2000){g->mining=0;g->finished=0;g->progress=0;g->next_left=now+250;}
            }
        }
    }else if(right && now>=g->next_right && !g->using_item){
        g->next_right=now+200;
        if(victim>=0){action(out,&n,MC_ACTION_INTERACT,0,0,victim,NULL);action(out,&n,MC_ACTION_SWING,0,0,0,NULL);}
        else{
            int hand=0;const McItem *use=held;
            if(!held->stack.count || (!mc_use_def(held->stack.item)->ticks && mc_use_def(inv->player[45].stack.item)->ticks)){use=&inv->player[45];hand=1;}
            if(targeted){action(out,&n,MC_ACTION_USE_BLOCK,0,hand,0,&target);action(out,&n,MC_ACTION_SWING,0,hand,0,NULL);}
            const McUseDef *def=mc_use_def(use->stack.item);
            if(use->stack.count && (!targeted || (mc_item_def(use->stack.item)->state<0 && !mc_dig_def(target.state)->interactive))){
                if(!def->food || def->always_eat || c->gamemode==1 || c->food<20){
                    action(out,&n,MC_ACTION_USE_AIR,0,hand,0,NULL);
                    if(def->ticks){g->using_item=1;g->using_ack=0;g->hand=hand;g->used_item=use->stack.item;g->use_started=now;}
                }
            }
        }
    }
    return n;
}
int mc_gameplay_send(McClient *c,const McGameAction *a){
    const McTarget *t=&a->target;
    switch(a->type){
    case MC_ACTION_DIG:return mc_dig(c,a->status,t->x,t->y,t->z,t->face);
    case MC_ACTION_USE_BLOCK:return mc_use_block(c,t->x,t->y,t->z,t->face,t->cursor,a->hand);
    case MC_ACTION_USE_AIR:return mc_use_air(c,a->hand);
    case MC_ACTION_RELEASE:return mc_release_use(c);
    case MC_ACTION_ATTACK:return mc_entity_action(c,a->entity,1,0,c->sneaking);
    case MC_ACTION_INTERACT:return mc_entity_action(c,a->entity,0,a->hand,c->sneaking);
    case MC_ACTION_SWING:return mc_swing(c,a->hand);
    default:return -1;
    }
}
