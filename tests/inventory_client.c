#include "inventory.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
static McInventory inventory;
static int handle(void *c,uint32_t id,const uint8_t *p,size_t n){return mc_inventory_packet(&inventory,c,id,p,n);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %d %s / %s\n",__LINE__,c.message,inventory.error);return 1;}}while(0)
int main(int argc,char **argv){
 McClient c;mc_init(&c);mc_inventory_init(&inventory);CHECK(argc>3 && net_init()==0);
 CHECK(mc_login(&c,argv[2],atoi(argv[3]),"VitaPlayer")==0);c.handler=handle;c.handler_context=&c;
 uint64_t end=net_millis()+3000;int phase=0;uint8_t wire[MC_ITEM_WIRE_MAX+32];
 while(net_millis()<end){
  CHECK(mc_step(&c)>=0);int n=0;
  if(phase==0 && c.position_ready){n=mc_inventory_click_packet(&inventory,0,36,0,0,net_millis(),wire,sizeof(wire));CHECK(n>0);phase++;}
  else if(phase==1 && inventory.carried.stack.count==5){n=mc_inventory_click_packet(&inventory,0,37,0,0,net_millis(),wire,sizeof(wire));CHECK(n>0);phase++;}
  else if(phase==2 && inventory.window_id==2 && inventory.slot_count==63){CHECK(c.hotbar[0].item==165);n=mc_inventory_click_packet(&inventory,2,0,0,1,net_millis(),wire,sizeof(wire));CHECK(n>0);phase++;}
  else if(phase==3 && !inventory.window_id && inventory.player[36].stack.item==1)break;
  if(n)CHECK(mc_send_play(&c,wire,n)==0);
 }
 CHECK(phase==3 && !inventory.window_id && inventory.player[36].stack.item==1 && inventory.clicks==3);
 mc_close(&c);mc_inventory_destroy(&inventory);net_term();puts("inventory clicks=3");return 0;
}
