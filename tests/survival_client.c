#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %d %s\n",__LINE__,c.message);return 1;}}while(0)
int main(int argc,char **argv){
 McClient c;mc_init(&c);CHECK(argc>3 && net_init()==0);CHECK(mc_login(&c,argv[2],atoi(argv[3]),"VitaPlayer")==0);
 uint64_t end=net_millis()+2500;int phase=0;
 while(net_millis()<end){
  CHECK(mc_step(&c)>=0);
  if(!phase && c.position_ready){
   CHECK(c.entity_id==42 && c.health_ready && c.health==18 && c.food==7 && c.experience_level==2 && c.experience==.5f);
   CHECK(fabs(c.attack_speed-1.6)<1e-6 && fabs(c.movement_speed-.13)<1e-6 && c.armor==5);
   CHECK(c.effect_until[3]>net_millis() && c.effect_amplifier[3]==1 && c.air==250 && c.using_item && c.using_offhand);
   CHECK(c.knockback_updates==1 && fabsf(c.knockback[0]-.4f)<1e-6);
   CHECK(mc_dig(&c,0,-4,-2,-7,1)==0 && mc_dig(&c,1,-4,-2,-7,1)==0 && mc_dig(&c,2,-4,-2,-7,1)==0);
   CHECK(mc_use_air(&c,1)==0 && mc_release_use(&c)==0 && mc_entity_action(&c,99,1,0,0)==0 && mc_entity_action(&c,99,0,1,1)==0);phase=1;
  }else if(phase==1 && c.health_ready && c.health==0){CHECK(mc_use_air(&c,0)==-1 && mc_respawn(&c)==0);phase=2;}
  else if(phase==2 && c.teleports==2 && c.health_ready && c.health==20)break;
 }
 CHECK(phase==2 && c.teleports==2 && c.death_updates==1 && !c.respawn_pending && !c.effect_until[3]);
 CHECK(c.interaction_sequence==4 && c.interaction_ack==4 && c.dig_finishes==1 && c.dig_cancels==1 && c.attack_packets==1 && c.use_packets==1);
 mc_close(&c);net_term();puts("survival actions=7 respawn=1");return 0;
}
