/* Host-controlled real-server gameplay fixture. Commands arrive over stdin;
   JSON state reports come only from decoded server state and shared physics. */
#define _POSIX_C_SOURCE 200809L
#include "gameplay.h"
#include "mesh.h"
#include "net.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
static McWorld world;static McInventory inventory;static McEntities entities;
static int handle(void *c,uint32_t id,const uint8_t *p,size_t n){
 if(mc_world_packet(&world,id,p,n)<0 || mc_entities_packet(&entities,id,p,n,net_millis())<0)return -1;
 return mc_inventory_packet(&inventory,c,id,p,n);
}
static void report(const McClient *c,const McPlayer *p,const McGameplay *g){
 printf("{\"ready\":%d,\"health\":%.3f,\"food\":%d,\"health_updates\":%llu,\"deaths\":%llu,\"teleports\":%llu,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"block\":%u,\"mining\":%d,\"progress\":%.3f,\"using\":%d,\"server_using\":%d,\"offhand\":%d,\"breaks\":%llu,\"finishes\":%llu,\"ack\":%u,\"uses\":%llu,\"attacks\":%llu,\"spawned\":%llu,\"removed\":%llu,\"knockback\":%llu,\"items\":[",c->position_ready && p->initialized && !p->waiting && c->health_ready,c->health,c->food,(unsigned long long)c->health_updates,(unsigned long long)c->death_updates,(unsigned long long)c->teleports,p->x,p->y,p->z,mc_world_get(&world,0,-60,2),g->mining,g->progress,g->using_item,c->using_item,c->using_offhand,(unsigned long long)c->break_packets,(unsigned long long)c->dig_finishes,c->interaction_ack,(unsigned long long)c->use_packets,(unsigned long long)c->attack_packets,(unsigned long long)entities.spawned,(unsigned long long)entities.removed,(unsigned long long)c->knockback_updates);
 int first=1;
 for(int i=0;i<46;i++)if(inventory.player[i].stack.count){printf("%s[%d,%d,%d]",first?"":",",inventory.player[i].stack.item,inventory.player[i].stack.count,i);first=0;}
 printf("],\"entities\":[");first=1;
 for(int i=0;i<MC_MAX_ENTITIES;i++){
  const McEntity *e=&entities.entries[i];if(!e->active)continue;
  printf("%s[%d,%d,%.3f,%.3f,%.3f,%.2f]",first?"":",",e->id,e->type,e->x,e->y,e->z,e->health);first=0;
 }
 puts("]}");fflush(stdout);
}
int main(int argc,char **argv){
 if(argc!=3 || net_init()<0)return 2;
 McClient c;mc_init(&c);mc_world_init(&world);mc_inventory_init(&inventory);mc_entities_init(&entities);mc_mesh_init();
 if(mc_login(&c,argv[1],atoi(argv[2]),"SurvivalProbe")<0)return 3;c.handler=handle;c.handler_context=&c;
 fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK);
 McPlayer player={0};McGameplay g;mc_gameplay_reset(&g);int left=0,right=0,moving=0,quit=0,result=1;double goal[3]={0};
 uint64_t end=net_millis()+180000,next=0,next_report=0,teleport=0,knockback=0;char input[1024];size_t used=0;
 while(!quit && net_millis()<end){
  ssize_t got=read(STDIN_FILENO,input+used,sizeof(input)-used-1);
  if(got>0){
   used+=got;input[used]=0;char *line=input,*newline;
   while((newline=strchr(line,'\n'))){
    *newline=0;
    if(!strcmp(line,"mine")){left=1;right=0;}
    else if(!strcmp(line,"use")){right=1;left=0;}
    else if(!strcmp(line,"idle")){left=right=moving=0;}
    else if(!strcmp(line,"respawn"))mc_respawn(&c);
    else if(!strcmp(line,"quit")){quit=1;result=0;}
    else if(sscanf(line,"move %lf %lf %lf",&goal[0],&goal[1],&goal[2])==3){left=right=0;moving=1;}
    line=newline+1;
   }
   used=strlen(line);memmove(input,line,used);if(used==sizeof(input)-1)used=0;
  }else if(got==0)break;
  if(mc_step(&c)<0){fprintf(stderr,"%s / %s / %s\n",c.message,world.error,inventory.error);break;}
  if(c.position_ready && c.teleports!=teleport){mc_player_reset(&player,c.x,c.y,c.z,c.yaw,c.pitch);mc_gameplay_reset(&g);teleport=c.teleports;knockback=c.knockback_updates;moving=0;next=net_millis();}
  if(c.health_ready && c.health<=0)mc_gameplay_reset(&g);
  if(c.position_ready && player.initialized && (!c.health_ready || c.health>0) && net_millis()>=next){
   McInput in={.yaw=c.yaw,.pitch=c.pitch};
   if(moving){double dx=goal[0]-player.x,dz=goal[2]-player.z,d=hypot(dx,dz);if(d<.05)moving=0;else {in.forward=fmin(1,d/.21585);in.yaw=atan2(-dx,dz)*57.295779513;in.pitch=0;}}
   player.speed_scale=c.movement_speed/.1;
   if(knockback!=c.knockback_updates){player.velocity_x=c.knockback[0];player.velocity_y=c.knockback[1];player.velocity_z=c.knockback[2];knockback=c.knockback_updates;}
   mc_player_tick(&player,&world,&in);if(!player.waiting)mc_move(&c,player.x,player.y,player.z,player.yaw,player.pitch,player.grounded);
   McGameAction actions[MC_GAME_ACTIONS];unsigned n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&player,!player.waiting,left,right,net_millis(),actions);
   for(unsigned i=0;i<n;i++)if(mc_gameplay_send(&c,&actions[i])<0)break;
   next=net_millis()+50;
  }
  if(net_millis()>=next_report){report(&c,&player,&g);next_report=net_millis()+250;}
 }
 mc_close(&c);mc_inventory_destroy(&inventory);mc_world_destroy(&world);net_term();return result;
}
