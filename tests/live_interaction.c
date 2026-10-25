/* Opt-in vanilla compatibility probe: places and removes one block beneath a
   dedicated creative player, checking actual inbound world updates and acks. */
#include "interaction.h"
#include "mesh.h"
#include "net.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int packet(void *w,uint32_t id,const uint8_t *p,size_t n){return mc_world_packet(w,id,p,n);}
int main(int argc,char **argv){
    if(argc!=3 || net_init()<0)return 2;
    mc_mesh_init();McWorld w;mc_world_init(&w);McClient c;mc_init(&c);
    int result=1,phase=0,x=0,y=0,z=0;uint64_t changed=0;
    if(mc_login(&c,argv[1],atoi(argv[2]),"InteractProbe")<0)return 2;
    c.handler=packet;c.handler_context=&w;
    uint64_t end=net_millis()+15000;
    while(net_millis()<end && mc_step(&c)>=0){
        if(!phase && c.position_ready && c.gamemode==1 && c.can_fly && mc_world_chunk(&w,(int)floor(c.x/16),(int)floor(c.z/16))){
            x=(int)floor(c.x);z=(int)floor(c.z);
            for(y=(int)floor(c.y)-1;y>w.min_y;y--){
                unsigned count;const McBlockBox *b=mc_block_boxes(mc_world_get(&w,x,y,z),&count);
                if(count==1 && b->bounds[0]==0 && b->bounds[1]==0 && b->bounds[2]==0 && b->bounds[3]==32 && b->bounds[4]==32 && b->bounds[5]==32)break;
            }
            if(y<=w.min_y || mc_world_get(&w,x,y+1,z))continue;
            if(mc_set_flying(&c,1)<0 || mc_move(&c,x+.5,y+4,z+.5,0,90,0)<0 || mc_select_slot(&c,2)<0 || mc_creative_slot(&c,2,1)<0)break;
            changed=w.updates;
            if(mc_interact(&c,&w,1)<0)break;
            if(mc_world_get(&w,x,y+1,z)!=0){fprintf(stderr,"Speculative world mutation\n");break;}
            phase=1;
        }
        if(phase==1 && mc_world_get(&w,x,y+1,z)==1 && c.interaction_ack==1){
            printf("Server confirmed placement at %d,%d,%d updates=%llu ack=%u\n",x,y+1,z,(unsigned long long)(w.updates-changed),c.interaction_ack);
            if(mc_interact(&c,&w,0)<0)break;phase=2;
        }
        if(phase==2 && mc_world_get(&w,x,y+1,z)==0 && c.interaction_ack==2){
            printf("Server confirmed removal; breaks=%llu places=%llu ack=%u\n",(unsigned long long)c.break_packets,(unsigned long long)c.place_packets,c.interaction_ack);
            result=0;break;
        }
    }
    printf("phase=%d mode=%d %s / %s\n",phase,c.gamemode,c.message,w.error);
    mc_close(&c);mc_world_destroy(&w);net_term();return result;
}
