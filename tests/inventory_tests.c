#include "inventory.h"
#include "inventory_ui.h"
#include "items.h"
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static uint8_t data[16384];static size_t length;
static void byte(unsigned x){data[length++]=x;}
static void vi(unsigned x){length+=mc_varint_write(data+length,x);}
static void shortint(unsigned x){byte(x>>8);byte(x);}
static void str(const char *s){vi(strlen(s));memcpy(data+length,s,strlen(s));length+=strlen(s);}
static void item(int id,int count,int nbt){
 if(!count){byte(0);return;}byte(1);vi(id);byte(count);
 if(!nbt){byte(0);return;}
 byte(10);shortint(0);byte(8);shortint(4);memcpy(data+length,"Name",4);length+=4;shortint(4);memcpy(data+length,"Test",4);length+=4;byte(0);
}
int main(void){
 McClient c;mc_init(&c);McInventory inv;mc_inventory_init(&inv);McInventoryView view;
 length=0;byte(0);vi(129);vi(46);for(int i=0;i<46;i++)item(23,i==36?12:0,1);item(0,0,0);
 CHECK(mc_inventory_packet(&inv,&c,0x12,data,length)==0 && c.hotbar[0].item==23 && inv.player_state_id==129 && inv.bytes>0);
 McItem saved=inv.player[36];size_t bytes=inv.bytes;length--;CHECK(mc_inventory_packet(&inv,&c,0x12,data,length)<0 && inv.bytes==bytes && inv.player[36].wire==saved.wire);
 length=0;byte(255);vi(0);shortint(-1);item(261,5,1);CHECK(mc_inventory_packet(&inv,&c,0x14,data,length)==0);
 uint8_t out[MC_ITEM_WIRE_MAX+32];int n=mc_inventory_click_packet(&inv,0,37,0,0,100,out,sizeof(out));
 CHECK(n>0 && out[0]==0x0b && out[1]==0 && out[2]==0x81 && out[3]==1);
 CHECK(out[n-inv.carried.size-1]==0 && !memcmp(out+n-inv.carried.size,inv.carried.wire,inv.carried.size));
 CHECK(mc_inventory_click_packet(&inv,0,37,0,0,101,out,sizeof(out))==0);
 CHECK(mc_inventory_click_packet(&inv,0,99,0,0,1200,out,sizeof(out))==-1);
 length=0;byte(254);vi(0);shortint(0);item(165,4,0);CHECK(mc_inventory_packet(&inv,&c,0x14,data,length)==0 && c.hotbar[0].item==165);
 length=0;byte(254);vi(0);shortint(39);item(820,1,0);CHECK(mc_inventory_packet(&inv,&c,0x14,data,length)==0 && inv.player[5].stack.item==820);
 length=0;vi(2);vi(2);str("{\"text\":\"Test chest\"}");CHECK(mc_inventory_packet(&inv,&c,0x30,data,length)==0 && !strcmp(inv.title,"Test chest"));
 length=0;byte(2);vi(7);vi(63);for(int i=0;i<63;i++)item(i==0?1:165,i==0?64:i==54?5:0,0);item(0,0,0);
 CHECK(mc_inventory_packet(&inv,&c,0x12,data,length)==0 && inv.container_count==27 && inv.slot_count==63 && c.hotbar[0].item==165 && c.hotbar[0].count==5);
 CHECK(inv.player[36].size==5 && inv.carried.size==0 && inv.bytes==14); // two item IDs: 1 and 165; retained helmet.
 mc_inventory_view(&inv,&view,1000);CHECK(view.slots[0].count==64 && view.slots[54].item==165 && view.state_id==7);
 McInventoryRect rects[MC_INVENTORY_SLOTS];unsigned slots=mc_inventory_layout(&view,rects);CHECK(slots==63 && mc_inventory_navigate(rects,slots,0,1,0)==1);
 CHECK(mc_inventory_click_packet(&inv,2,0,0,1,1200,out,sizeof(out))>0);
 for(unsigned i=0;i<slots;i++)CHECK(rects[i].x>=0 && rects[i].y>=0 && rects[i].y+rects[i].size<=510);
 uint8_t close[2];CHECK(mc_inventory_close_packet(&inv,close)==2 && close[0]==0x0c && close[1]==2 && !inv.window_id && inv.player[36].stack.item==165);
 CHECK(inv.bytes==10);CHECK(mc_inventory_plain_hotbar(&inv,1,795,1)==0 && inv.player[37].stack.count==1);CHECK(mc_inventory_plain_hotbar(&inv,1,795,64)<0);
 mc_inventory_view(&inv,&view,2000);slots=mc_inventory_layout(&view,rects);CHECK(slots==46 && mc_inventory_navigate(rects,slots,36,1,0)==37);
 mc_inventory_destroy(&inv);CHECK(inv.bytes==0);
 mc_mesh_init();unsigned count=mc_items_count();CHECK(count>1200 && count<4096 && mc_item_def(795)->stack_size==1 && !strcmp(mc_item_def(1)->display,"Stone"));
 uint32_t *pixels=malloc((MC_ITEM_ATLAS_SIZE*MC_ITEM_ATLAS_SIZE+1)*4);CHECK(pixels);pixels[MC_ITEM_ATLAS_SIZE*MC_ITEM_ATLAS_SIZE]=0x12345678;
 CHECK(mc_item_textures_build(pixels)==0 && pixels[MC_ITEM_ATLAS_SIZE*MC_ITEM_ATLAS_SIZE]==0x12345678);
 unsigned stone=0,pick=0;for(int y=0;y<16;y++)for(int x=0;x<16;x++){stone+=!!pixels[y*MC_ITEM_ATLAS_SIZE+16+x];pick+=!!pixels[(795/64*16+y)*MC_ITEM_ATLAS_SIZE+795%64*16+x];}
 CHECK(stone>100 && pick>10 && pick<100);free(pixels);
 puts("Inventory tests passed: NBT/cursor preservation, state IDs, pending clicks, chest/player mapping, memory ownership, UI navigation and item atlas bounds");
}
