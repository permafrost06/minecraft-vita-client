#include "protocol.h"
#include "net.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
int main(int argc,char **argv){
    if(argc<4 || net_init()<0)return 2;
    McClient c;mc_init(&c);
    if(mc_move(&c,0,0,0,0,0,1)!=-1)return 3;
    if(mc_login(&c,argv[2],atoi(argv[3]),"VitaPlayer")<0)return 4;
    uint64_t end=net_millis()+1000;int result=0;
    while(net_millis()<end){
        if(mc_step(&c)<0){result=5;break;}
        if(c.teleports==1 && c.movement_packets==0){
            if(mc_set_movement_flags(&c,1,1)<0 || mc_set_movement_flags(&c,1,1)<0 || mc_set_movement_flags(&c,0,0)<0 || mc_set_flying(&c,1)<0 || mc_move(&c,20,70,-7,120,15,1)<0){result=6;break;}
        }
    }
    if(c.teleports!=2 || c.movement_packets!=1 || !c.position_ready || c.can_fly || c.flying ||
       fabs(c.x-21)>1e-8 || fabs(c.y-72)>1e-8 || fabs(c.z+4)>1e-8 || c.yaw!=130 || c.pitch!=20)result=7;
    if(mc_move(&c,NAN,70,0,0,0,0)!=-1 || mc_set_flying(&c,1)!=-1)result=8;
    printf("movement=%llu teleports=%llu position=%.1f,%.1f,%.1f yaw=%.0f flight=%d\n",(unsigned long long)c.movement_packets,(unsigned long long)c.teleports,c.x,c.y,c.z,c.yaw,c.flying);
    mc_close(&c);net_term();return result;
}
