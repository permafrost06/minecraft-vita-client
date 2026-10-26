/* Interactive, opt-in vanilla fixture for chat/sound/workstation packet paths. */
#include "chat.h"
#include "sound.h"
#include "inventory_ui.h"
#include "net.h"
#include "interaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
static McInventory inv;static McChat chat;static McSound sound;static McEntities entities;
static int handle(void *ctx,uint32_t id,const uint8_t *p,size_t n){
 McClient *c=ctx;
 if(mc_entities_packet(&entities,id,p,n,net_millis())<0 || mc_inventory_packet(&inv,c,id,p,n)<0 || mc_chat_packet(&chat,id,p,n,net_millis())<0 || mc_sound_packet(&sound,&entities,c,id,p,n)<0)return -1;
 return 0;
}
int main(int argc,char **argv){
 if(argc!=3 || net_init()<0)return 2;
 McClient c;mc_init(&c);mc_inventory_init(&inv);mc_entities_init(&entities);mc_chat_init(&chat);mc_sound_init(&sound);
 if(mc_login(&c,argv[1],atoi(argv[2]),"SocialProbe")<0)return 3;c.handler=handle;c.handler_context=&c;
 uint64_t end=net_millis()+180000,next=0;uint8_t wire[MC_ITEM_WIRE_MAX+32];int running=1;
 while(running && net_millis()<end){
  if(mc_step(&c)<0){fprintf(stderr,"%s %s\n",c.message,inv.error);break;}
  struct pollfd input={0,POLLIN,0};
  if(poll(&input,1,0)>0 && (input.revents&POLLIN)){
   char line[512];if(!fgets(line,sizeof(line),stdin))break;line[strcspn(line,"\r\n")]=0;int n=0,a=0,b=0,d=0;
   if(!strcmp(line,"quit")){running=0;continue;}
   else if(sscanf(line,"open %d %d %d",&a,&b,&d)==3){float cursor[3]={.5f,.5f,0};mc_use_block(&c,a,b,d,2,cursor,0);}
   else if(!strcmp(line,"trade")){for(int i=0;i<MC_MAX_ENTITIES;i++)if(entities.entries[i].active && !strcmp(mc_entity_def(entities.entries[i].type)->name,"Villager")){mc_entity_action(&c,entities.entries[i].id,0,0,0);break;}}
   else if(sscanf(line,"click %d %d %d",&a,&b,&d)==3)n=mc_inventory_click_packet(&inv,inv.window_id,a,b,d,net_millis(),wire,sizeof(wire));
   else if(sscanf(line,"button %d",&a)==1)n=mc_inventory_button_packet(&inv,inv.window_id,a,net_millis(),wire);
   else if(!strncmp(line,"rename ",7))n=mc_inventory_rename_packet(&inv,inv.window_id,line+7,wire,sizeof(wire));
   else if(!strncmp(line,"say ",4)){n=mc_chat_encode(line+4,(uint64_t)time(NULL)*1000,chat.signed_pending,wire,sizeof(wire));chat.signed_pending=0;}
   else if(!strcmp(line,"close"))n=mc_inventory_close_packet(&inv,wire);
   if(n>0)mc_send_play(&c,wire,n);
  }
  if(net_millis()>=next){
   McInventoryView v;mc_inventory_view(&inv,&v,net_millis());
   printf("{\"ready\":%d,\"window\":%d,\"type\":%d,\"chat\":%u,\"sounds\":%llu,\"recipes\":%u,\"options\":%d,\"carried\":[%d,%d],\"slots\":[",c.position_ready,v.window_id,v.window_type,chat.count,(unsigned long long)sound.played,inv.recipes?inv.recipes->count:0,mc_inventory_option_count(&v),v.carried.item,v.carried.count);
   for(int i=0;i<v.slot_count;i++)printf("%s[%d,%d]",i?",":"",v.slots[i].item,v.slots[i].count);
   printf("],\"properties\":[");for(int i=0;i<16;i++)printf("%s%d",i?",":"",v.properties[i]);printf("],\"trades\":%d,\"choices\":[",v.trade_count);for(int i=0;i<v.stone_count;i++)printf("%s[%d,%d]",i?",":"",v.stone_choices[i].item,v.stone_choices[i].count);printf("]}\n");fflush(stdout);next=net_millis()+100;
  }
 }
 mc_close(&c);mc_inventory_destroy(&inv);net_term();return running?1:0;
}
