/* Opt-in second-player fixture for real-server entity tracking/rendering. It
   flies above terrain to a requested point, equips stone, then moves nearby. */
#include "protocol.h"
#include "entities.h"
#include "net.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static McEntities entities;
static int packet(void *unused,uint32_t id,const uint8_t *p,size_t n){(void)unused;return mc_entities_packet(&entities,id,p,n,net_millis());}
int main(int argc,char **argv){
    if(argc<6 || net_init()<0)return 2;
    double goal[3]={atof(argv[3]),atof(argv[4]),atof(argv[5])};
    McClient c;mc_init(&c);mc_entities_init(&entities);
    if(mc_login(&c,argv[1],atoi(argv[2]),"EntityActor")<0)return 3;
    c.handler=packet;
    uint64_t end=net_millis()+90000,next=0,ready=0;int phase=0,equipped=0;double cruise=0;
    while(net_millis()<end){
        if(mc_step(&c)<0){fprintf(stderr,"%s / %s\n",c.message,entities.error);break;}
        if(!c.position_ready || !c.can_fly)continue;
        if(!c.flying && mc_set_flying(&c,1)<0)break;
        if(!equipped){if(mc_select_slot(&c,0)<0 || mc_creative_slot(&c,0,1)<0)break;equipped=1;cruise=fmax(c.y,goal[1])+48;}
        if(net_millis()<next)continue;next=net_millis()+50;
        double target[3]={goal[0],goal[1],goal[2]};
        if(phase==0){target[0]=c.x;target[1]=cruise;target[2]=c.z;}
        else if(phase==1)target[1]=cruise;
        else if(phase>=3){double t=(net_millis()-ready)*.001;target[0]+=sin(t)*1.5;target[2]+=cos(t)*1.5;}
        double dx=target[0]-c.x,dy=target[1]-c.y,dz=target[2]-c.z,d=sqrt(dx*dx+dy*dy+dz*dz),f=d>.5?.5/d:1;
        if(mc_move(&c,c.x+dx*f,c.y+dy*f,c.z+dz*f,180,0,0)<0)break;
        if(d<=.5 && phase<3){phase++;if(phase==3){ready=net_millis();printf("READY position=%.2f,%.2f,%.2f entities=%d\n",c.x,c.y,c.z,entities.count);fflush(stdout);}}
        if(ready && net_millis()-ready>30000){mc_close(&c);net_term();return 0;}
    }
    fprintf(stderr,"actor phase=%d teleports=%llu %s\n",phase,(unsigned long long)c.teleports,c.message);mc_close(&c);net_term();return 1;
}
