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
static int sample[3]={0,-60,0};
static McWorld world;static McInventory inventory;static McEntities entities;
static int handle(void *c,uint32_t id,const uint8_t *p,size_t n){
 if(mc_world_packet(&world,id,p,n)<0 || mc_entities_packet(&entities,id,p,n,net_millis())<0)return -1;
 return mc_inventory_packet(&inventory,c,id,p,n);
}
static void report(const McClient *c,const McPlayer *p,const McGameplay *g){
 printf("{\"ready\":%d,\"health\":%.3f,\"food\":%d,\"health_updates\":%llu,\"deaths\":%llu,\"teleports\":%llu,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"block\":%u,\"mining\":%d,\"progress\":%.3f,\"using\":%d,\"server_using\":%d,\"offhand\":%d,\"breaks\":%llu,\"finishes\":%llu,\"ack\":%u,\"uses\":%llu,\"attacks\":%llu,\"spawned\":%llu,\"removed\":%llu,\"knockback\":%llu,\"items\":[",c->position_ready && p->initialized && !p->waiting && c->health_ready,c->health,c->food,(unsigned long long)c->health_updates,(unsigned long long)c->death_updates,(unsigned long long)c->teleports,p->x,p->y,p->z,mc_world_get(&world,0,-60,2),g->mining,g->progress,g->using_item,c->using_item,c->using_offhand,(unsigned long long)c->break_packets,(unsigned long long)c->dig_finishes,c->interaction_ack,(unsigned long long)c->use_packets,(unsigned long long)c->attack_packets,(unsigned long long)entities.spawned,(unsigned long long)entities.removed,(unsigned long long)c->knockback_updates);
 int first=1;
 for(int i=0;i<46;i++)if(inventory.player[i].stack.count){printf("%s[%d,%d,%d]",first?"":",",inventory.player[i].stack.item,inventory.player[i].stack.count,i);first=0;}
 printf("],\"sample_light\":%d,\"light_updates\":%llu",mc_world_light(&world,sample[0],sample[1],sample[2]),(unsigned long long)world.light_updates);
 McLighting lighting=mc_environment_lighting(&world.environment,net_millis());
 int light=mc_world_light(&world,sample[0],sample[1],sample[2]);
 const McBlockEntity *be=mc_world_block_entity(&world,sample[0],sample[1],sample[2]);
 printf(",\"day_time\":%.2f,\"sky_factor\":%.4f,\"rain\":%.3f,\"thunder\":%.3f,\"sample_brightness\":%.4f,\"sample_entity_type\":%d,\"sample_entity_bytes\":%u,\"block_entity_updates\":%llu",mc_environment_time(&world.environment,net_millis()),lighting.sky,world.environment.rain,world.environment.thunder,light<0?-1:mc_light_brightness((light&15)/15.0f,(light>>4)/15.0f,&lighting),be?(int)be->type:-1,be?(unsigned)be->size:0,(unsigned long long)world.block_entity_updates);
 printf(",\"window\":%d,\"sample_entity_action\":%d,\"sample_entity_parameter\":%d",inventory.window_id,be?be->action:0,be?be->parameter:0);
 char sign[128];int sign_ok=mc_block_entity_sign_line(be,0,sign,sizeof(sign))==0;
 printf(",\"sample_text\":\"");if(sign_ok)for(const char *s=sign;*s;s++){if(*s=='\\' || *s=='"')putchar('\\');putchar(*s);}printf("\"");
 printf(",\"dimension\":\"%s\",\"min_y\":%d,\"height\":%d,\"sneak\":%d,\"sprint\":%d,\"water\":%d,\"eye\":%.3f,\"ticks\":%llu,\"border\":%.3f,\"entities\":[",world.dimension,world.min_y,world.height,p->sneaking,p->sprinting,p->in_water,p->eye,(unsigned long long)p->ticks,mc_world_border_size(&world,net_millis()));first=1;
 for(int i=0;i<MC_MAX_ENTITIES;i++){
  const McEntity *e=&entities.entries[i];if(!e->active)continue;
  printf("%s[%d,%d,%.3f,%.3f,%.3f,%.2f]",first?"":",",e->id,e->type,e->x,e->y,e->z,e->health);first=0;
 }
 printf("],\"sleeping\":%d,\"pose\":%d,\"gliding\":%d}\n",p->sleeping,c->pose,p->gliding);fflush(stdout);
}
int main(int argc,char **argv){
 if(argc!=3 || net_init()<0)return 2;
 McClient c;mc_init(&c);mc_world_init(&world);mc_inventory_init(&inventory);mc_entities_init(&entities);mc_mesh_init();
 if(mc_login(&c,argv[1],atoi(argv[2]),"SurvivalProbe")<0)return 3;c.handler=handle;c.handler_context=&c;
 fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK);
 McPlayer player={0};McGameplay g;mc_gameplay_reset(&g);int left=0,right=0,moving=0,quit=0,result=1;double goal[3]={0};McInput controls={0};
 uint64_t end=net_millis()+180000,next=0,next_report=0,teleport=0,knockback=0;char input[1024];size_t used=0;
 while(!quit && net_millis()<end){
  ssize_t got=read(STDIN_FILENO,input+used,sizeof(input)-used-1);
  if(got>0){
   used+=got;input[used]=0;char *line=input,*newline;
   while((newline=strchr(line,'\n'))){
    *newline=0;
    if(!strcmp(line,"mine")){left=1;right=0;}
    else if(!strcmp(line,"use")){right=1;left=0;}
    else if(!strcmp(line,"idle")){left=right=moving=0;controls=(McInput){0};}
    else if(sscanf(line,"input %f %d %d %d",&controls.forward,&controls.sneak,&controls.sprint,&controls.jump)==4){left=right=moving=0;}
    else if(sscanf(line,"sample %d %d %d",&sample[0],&sample[1],&sample[2])==3){}
    else if(!strcmp(line,"open")){float cursor[3]={.5f,.5f,.5f};mc_use_block(&c,sample[0],sample[1],sample[2],2,cursor,0);}
    else if(!strcmp(line,"close")){uint8_t wire[2];mc_inventory_close_packet(&inventory,wire);mc_send_play(&c,wire,2);}
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
   McInput in=controls;in.yaw=c.yaw;in.pitch=c.pitch;
   if(moving){double dx=goal[0]-player.x,dz=goal[2]-player.z,d=hypot(dx,dz);if(d<.05)moving=0;else {in.forward=fmin(1,d/.21585);in.yaw=atan2(-dx,dz)*57.295779513;in.pitch=0;}}
   mc_gameplay_movement(&c,&inventory,&player,&in,net_millis());
   if(knockback!=c.knockback_updates){player.velocity_x=c.knockback[0];player.velocity_y=c.knockback[1];player.velocity_z=c.knockback[2];knockback=c.knockback_updates;}
   mc_player_tick(&player,&world,&in);c.eye_height=player.eye;mc_set_movement_flags(&c,player.sneaking,player.sprinting);if(!player.waiting && !player.sleeping)mc_move(&c,player.x,player.y,player.z,player.yaw,player.pitch,player.grounded);
   McGameAction actions[MC_GAME_ACTIONS];unsigned n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&player,!player.waiting,left,right,net_millis(),actions);
   for(unsigned i=0;i<n;i++)if(mc_gameplay_send(&c,&actions[i])<0)break;
   next=net_millis()+50;
  }
  if(net_millis()>=next_report){report(&c,&player,&g);next_report=net_millis()+250;}
 }
 mc_close(&c);mc_inventory_destroy(&inventory);mc_world_destroy(&world);net_term();return result;
}
