/* Opt-in vanilla inventory/container probe using a dedicated creative account.
   The temporary chest is emptied and removed before success is reported. */
#include "inventory.h"
#include "interaction.h"
#include "items.h"
#include "mesh.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static McInventory inventory;static McWorld world;
static int handle(void *client,uint32_t id,const uint8_t *p,size_t n){
 if(mc_world_packet(&world,id,p,n)<0)return -1;
 return mc_inventory_packet(&inventory,client,id,p,n);
}
static int click(McClient *c,int slot,int button,int mode){
 uint8_t *wire=malloc(MC_ITEM_WIRE_MAX+32);if(!wire)return -1;
 int n=mc_inventory_click_packet(&inventory,inventory.window_id,slot,button,mode,net_millis(),wire,MC_ITEM_WIRE_MAX+32);
 int result=n>0?mc_send_play(c,wire,n):-1;free(wire);return result;
}
static int reset_items(McClient *c){
 for(int slot=9;slot<45;slot++){uint8_t wire[4]={0x2b,0,(uint8_t)slot,0};if(mc_send_play(c,wire,4)<0)return -1;}
 return 0;
}
static int returned_stone(void){for(int i=9;i<45;i++)if(inventory.player[i].stack.item==1 && inventory.player[i].stack.count==63)return i;return -1;}
static int chest(int x,int y,int z){return mc_block_def(mc_world_get(&world,x,y,z))->low==mc_block_def(mc_item_def(275)->state)->low;}
int main(int argc,char **argv){
 if(argc!=3 || net_init()<0)return 2;
 McClient c;mc_init(&c);mc_world_init(&world);mc_inventory_init(&inventory);mc_mesh_init();
 if(mc_login(&c,argv[1],atoi(argv[2]),"InventoryProbe")<0)return 3;c.handler=handle;c.handler_context=&c;
 int phase=0,result=1,bx=0,by=0,bz=0,crafted=0;uint16_t floor_state=0;uint64_t end=net_millis()+60000;
 while(net_millis()<end){
  if(mc_step(&c)<0){fprintf(stderr,"%s / %s / %s\n",c.message,world.error,inventory.error);break;}
  if(phase==0 && c.position_ready && c.gamemode==1 && c.can_fly && inventory.revision){
   if(reset_items(&c)<0 || mc_creative_slot(&c,0,1)<0 || mc_creative_slot(&c,1,15)<0)break;
   mc_inventory_plain_hotbar(&inventory,0,1,64);mc_inventory_plain_hotbar(&inventory,1,15,64);
   if(click(&c,36,0,0)<0)break;phase=1;
  }else if(phase==1 && inventory.carried.stack.item==1 && inventory.carried.stack.count==64 && !inventory.player[36].stack.count){if(click(&c,37,0,0)<0)break;phase=2;}
  else if(phase==2 && inventory.carried.stack.item==15 && inventory.player[37].stack.item==1){if(click(&c,36,0,0)<0)break;phase=3;}
  else if(phase==3 && !inventory.carried.stack.count && inventory.player[36].stack.item==15){if(click(&c,37,1,0)<0)break;phase=4;}
  else if(phase==4 && inventory.carried.stack.count==32 && inventory.player[37].stack.count==32){if(click(&c,38,1,0)<0)break;phase=5;}
  else if(phase==5 && inventory.carried.stack.count==31 && inventory.player[38].stack.count==1){if(click(&c,37,0,0)<0)break;phase=6;}
  else if(phase==6 && !inventory.carried.stack.count && inventory.player[37].stack.count==63){
   printf("Server confirmed pickup, swap, split and merge: hotbar dirt=%d stone=%d+%d\n",inventory.player[36].stack.count,inventory.player[37].stack.count,inventory.player[38].stack.count);fflush(stdout);
   if(!crafted){
    if(mc_creative_item(&c,3,109,1)<0)break;mc_inventory_plain_hotbar(&inventory,3,109,1);
    if(click(&c,39,0,0)<0)break;phase=20;continue;
   }
   int found=0;
   for(int dz=-2;dz<=2 && !found;dz++)for(int dx=-2;dx<=2 && !found;dx++){
    int x=(int)floor(c.x)+dx,z=(int)floor(c.z)+dz;
    for(int y=(int)floor(c.y);y>world.min_y;y--){
     unsigned count;const McBlockBox *b=mc_block_boxes(mc_world_get(&world,x,y,z),&count);
     if(count==1 && b->bounds[0]==0 && b->bounds[1]==0 && b->bounds[2]==0 && b->bounds[3]==32 && b->bounds[4]==32 && b->bounds[5]==32){
      if(!mc_world_get(&world,x,y+1,z)){bx=x;by=y+1;bz=z;floor_state=mc_world_get(&world,x,y,z);found=1;}break;
     }
    }
   }
   if(!found){fprintf(stderr,"No clear test surface\n");break;}
   if(mc_set_flying(&c,1)<0 || mc_move(&c,bx+.5,by+3,bz+.5,0,90,0)<0 || mc_select_slot(&c,2)<0 || mc_creative_slot(&c,2,275)<0)break;
   mc_inventory_plain_hotbar(&inventory,2,275,64);if(mc_interact(&c,&world,1)<0)break;phase=7;
  }else if(phase==7 && chest(bx,by,bz)){if(mc_interact(&c,&world,1)<0)break;phase=8;}
  else if(phase==8 && inventory.window_id && inventory.container_count==27 && inventory.slot_count==63){if(click(&c,55,0,1)<0)break;phase=9;}
  else if(phase==9 && inventory.container[0].stack.item==1 && inventory.container[0].stack.count==63 && !inventory.player[37].stack.count){if(click(&c,0,0,1)<0)break;phase=10;}
  else if(phase==10 && !inventory.container[0].stack.count && returned_stone()>=0){
   printf("Server confirmed chest transfers: returned stone=%d slot=%d, clicks=%llu\n",inventory.player[returned_stone()].stack.count,returned_stone(),(unsigned long long)inventory.clicks);fflush(stdout);
   uint8_t wire[2];mc_inventory_close_packet(&inventory,wire);if(mc_send_play(&c,wire,2)<0 || mc_interact(&c,&world,0)<0)break;phase=11;
  }else if(phase==11 && !mc_world_get(&world,bx,by,bz) && mc_world_get(&world,bx,by-1,bz)==floor_state && c.interaction_ack==3){
   if(mc_creative_item(&c,2,278,1)<0 || mc_creative_item(&c,5,765,1)<0 || mc_creative_item(&c,6,758,1)<0)break;
   mc_inventory_plain_hotbar(&inventory,2,278,1);mc_inventory_plain_hotbar(&inventory,5,765,1);mc_inventory_plain_hotbar(&inventory,6,758,1);
   if(mc_interact(&c,&world,1)<0)break;phase=30;
  }else if(phase==20 && inventory.carried.stack.item==109 && inventory.carried.stack.count==1){if(click(&c,1,0,0)<0)break;phase=21;}
  else if(phase==21 && !inventory.carried.stack.count && inventory.player[0].stack.item==23 && inventory.player[0].stack.count==4){if(click(&c,0,0,0)<0)break;phase=22;}
  else if(phase==22 && inventory.carried.stack.item==23 && inventory.carried.stack.count==4 && !inventory.player[1].stack.count){if(click(&c,40,0,0)<0)break;phase=23;}
  else if(phase==23 && !inventory.carried.stack.count && inventory.player[40].stack.item==23 && inventory.player[40].stack.count==4){printf("Server confirmed crafting: 1 oak log -> 4 oak planks\n");fflush(stdout);crafted=1;phase=6;}
  else if(phase==30 && mc_block_def(mc_world_get(&world,bx,by,bz))->low==mc_block_def(mc_item_def(278)->state)->low){if(mc_interact(&c,&world,1)<0)break;phase=31;}
  else if(phase==31 && inventory.window_id && inventory.window_type==13 && inventory.slot_count==39){if(click(&c,35,0,0)<0)break;phase=32;}
  else if(phase==32 && inventory.carried.stack.item==765){if(click(&c,0,0,0)<0)break;phase=33;}
  else if(phase==33 && !inventory.carried.stack.count && inventory.container[0].stack.item==765){if(click(&c,36,0,0)<0)break;phase=34;}
  else if(phase==34 && inventory.carried.stack.item==758){if(click(&c,1,0,0)<0)break;phase=35;}
  else if(phase==35 && !inventory.carried.stack.count && inventory.container[2].stack.item==766 && inventory.container[2].stack.count==1){if(click(&c,2,0,1)<0)break;phase=36;}
  else if(phase==36 && !inventory.container[2].stack.count){
   int found=0;for(int i=9;i<45;i++)if(inventory.player[i].stack.item==766 && inventory.player[i].stack.count==1)found=1;
   if(!found)continue;
   printf("Server confirmed smelting: raw iron + coal -> iron ingot\n");fflush(stdout);
   uint8_t wire[2];mc_inventory_close_packet(&inventory,wire);if(mc_send_play(&c,wire,2)<0 || mc_interact(&c,&world,0)<0)break;phase=37;
  }else if(phase==37 && !mc_world_get(&world,bx,by,bz) && mc_world_get(&world,bx,by-1,bz)==floor_state && c.interaction_ack==6){result=0;break;}
 }
 if(result && (phase>=7 && phase<20 || phase>=30) && c.fd>=0 && (chest(bx,by,bz) || mc_block_def(mc_world_get(&world,bx,by,bz))->low==mc_block_def(mc_item_def(278)->state)->low)){
  uint8_t wire[2];mc_inventory_close_packet(&inventory,wire);mc_send_play(&c,wire,2);float cursor[3]={.5f,1,.5f};mc_block_action(&c,0,bx,by,bz,1,cursor);
  uint64_t cleanup=net_millis()+2000;while(net_millis()<cleanup && mc_world_get(&world,bx,by,bz))if(mc_step(&c)<0)break;
 }
 printf("inventory phase=%d clicks=%llu ack=%u temporary_block=%u %s\n",phase,(unsigned long long)inventory.clicks,c.interaction_ack,mc_world_get(&world,bx,by,bz),c.message);
 mc_close(&c);mc_inventory_destroy(&inventory);mc_world_destroy(&world);net_term();return result;
}
