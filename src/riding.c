#include "riding.h"
#include <math.h>
int mc_riding_tick(McClient *c,McEntities *entities,McPlayer *p,const McWorld *w,const McInput *input){
    const McEntity *e=mc_entity_get(entities,entities->local_vehicle);
    if(!e && entities->local_vehicle>=0){
        /* A passenger list can precede the spawn, or the entity cache can fill. */
        p->vehicle_id=entities->local_vehicle;p->boat=0;p->waiting=1;p->velocity_x=p->velocity_y=p->velocity_z=0;
        return mc_rider_input(c,0,0,0,input->sneak,input->yaw,input->pitch)<0?-1:1;
    }
    if(!e){
        if(p->vehicle_id>=0){float yaw=input->yaw,pitch=input->pitch;mc_player_reset(p,c->x,c->y,c->z,yaw,pitch);}
        return 0;
    }
    int boat=(e->type==9 || e->type==13) && entities->local_driver;
    if(p->vehicle_id!=e->id || p->boat!=boat){
        p->x=e->x;p->y=e->y;p->z=e->z;p->velocity_x=p->velocity_y=p->velocity_z=0;
        p->boat_yaw=e->yaw;p->boat_turn=0;p->vehicle_id=e->id;p->vehicle_updates=c->vehicle_updates;
    }
    p->boat=boat;p->flying=p->gliding=p->sleeping=0;
    if(boat){
        if(p->vehicle_updates!=c->vehicle_updates){
            p->x=c->vehicle_position[0];p->y=c->vehicle_position[1];p->z=c->vehicle_position[2];p->boat_yaw=c->vehicle_yaw;
            p->velocity_x=p->velocity_y=p->velocity_z=0;p->boat_turn=0;p->vehicle_updates=c->vehicle_updates;
        }
        mc_player_tick(p,w,input);
        if(!p->waiting){
            if(mc_vehicle_move(c,p->x,p->y,p->z,p->boat_yaw,0)<0 || mc_boat_paddles(c,input->forward>0 || input->right>0,input->forward>0 || input->right<0)<0)return -1;
            for(int i=0;i<MC_MAX_ENTITIES;i++)if(entities->entries[i].active && entities->entries[i].id==e->id){
                McEntity *moving=&entities->entries[i];moving->x=p->x;moving->y=p->y;moving->z=p->z;moving->yaw=p->boat_yaw;moving->from[0]=p->x;moving->from[1]=p->y;moving->from[2]=p->z;break;
            }
        }
    }else{
        p->x=e->x;p->y=e->y+.35;p->z=e->z;p->yaw=input->yaw;p->pitch=input->pitch;
        p->velocity_x=p->velocity_y=p->velocity_z=0;p->height=1.8f;p->eye=1.27f;p->grounded=p->waiting=p->sneaking=p->sprinting=p->swimming=0;p->ticks++;
    }
    c->x=p->x;c->y=p->y;c->z=p->z;c->eye_height=p->eye;
    return mc_rider_input(c,-fmaxf(-1,fminf(1,input->right)),fmaxf(-1,fminf(1,input->forward)),input->jump,input->sneak,input->yaw,input->pitch)<0?-1:1;
}
