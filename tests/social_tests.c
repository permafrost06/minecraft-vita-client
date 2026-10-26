#include "chat.h"
#include "text.h"
#include "sound.h"
#include "inventory.h"
#include "inventory_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char p[8192];static size_t n;
static void b(unsigned x){p[n++]=x;}
static void vi(unsigned x){n+=mc_varint_write(p+n,x);}
static void u32(unsigned x){b(x>>24);b(x>>16);b(x>>8);b(x);}
static void f32(float f){unsigned x;memcpy(&x,&f,4);u32(x);}
static void str(const char *s){vi(strlen(s));memcpy(p+n,s,strlen(s));n+=strlen(s);}
static void slot(int id,int count){b(1);vi(id);b(count);b(0);}
int main(void){
 char text[384];const char *json="{\"text\":\"Hello \",\"extra\":[{\"text\":\"world\\n!\"}]}";
 CHECK(mc_text_plain((const uint8_t *)json,strlen(json),text,sizeof(text))==0 && !strcmp(text,"Hello world !"));
 CHECK(mc_text_plain((const uint8_t *)"{\"text\":",8,text,sizeof(text))<0);
 const char *translated="{\"with\":[{\"translate\":\"gameMode.creative\"}],\"translate\":\"gamemode.changed\"}";
 CHECK(mc_text_plain((const uint8_t *)translated,strlen(translated),text,sizeof(text))==0 && !strcmp(text,"Your game mode is now Creative Mode"));
 McChat chat;mc_chat_init(&chat);n=0;str(json);b(0);CHECK(mc_chat_packet(&chat,0x64,p,n,100)==0 && chat.count==1);
 CHECK(mc_chat_packet(&chat,0x64,p,n-1,101)<0 && chat.count==1);
 p[n-1]=1;CHECK(mc_chat_packet(&chat,0x64,p,n,102)==0 && chat.count==1 && chat.action_at==102);
 n=0;str("{\"text\":\"Hi\"}");vi(0);str("{\"text\":\"Alex\"}");b(0);
 CHECK(mc_chat_packet(&chat,0x1b,p,n,103)==0 && !strcmp(chat.lines[1].text,"<Alex> Hi"));
 n=0;memset(p,0,16);n=16;vi(0);b(1);memset(p+n,0,256);n+=256;str("player chat");memset(p+n,0,16);n+=16;vi(0);b(0);vi(0);vi(0);str("\"Steve\"");b(0);
 CHECK(mc_chat_packet(&chat,0x35,p,n,104)==0 && chat.signed_pending==1 && !strcmp(chat.lines[2].text,"<Steve> player chat"));
 for(int i=0;i<40;i++)CHECK(mc_chat_packet(&chat,0x35,p,n,105)==0);
 CHECK(chat.count==32 && chat.signed_pending==41);
 int size=mc_chat_encode("/time set noon",0x0102030405060708ULL,41,p,sizeof(p));CHECK(size>0 && p[0]==4);
 McReader r={p,(size_t)size,1,0};CHECK(mc_string(&r,text,sizeof(text))==0 && !strcmp(text,"time set noon"));CHECK(mc_u64(&r)==0x0102030405060708ULL && mc_u64(&r)==0 && mc_u8(&r)==0 && mc_varint_read(&r)==41);CHECK(r.n-r.pos==3);
 CHECK(mc_chat_encode("hello",1,0,p,sizeof(p))>0 && p[0]==5);CHECK(mc_chat_encode("/",1,0,p,sizeof(p))<0);CHECK(mc_chat_encode("bad\ntext",1,0,p,sizeof(p))<0);
 McSound sound;mc_sound_init(&sound);McClient c;mc_init(&c);McEntities entities;mc_entities_init(&entities);
 n=0;vi(0);str("minecraft:block.stone.break");b(0);vi(4);u32(0);u32(0);u32(0);f32(1);f32(1);memset(p+n,0,8);n+=8;
 CHECK(mc_sound_packet(&sound,&entities,&c,0x62,p,n)==0 && sound.played==1);
 CHECK(mc_sound_packet(&sound,&entities,&c,0x62,p,n-1)<0 && sound.played==1);
 int16_t pcm[2048];mc_sound_mix(&sound,pcm,1024,0,0,0,0);unsigned audible=0;for(int i=0;i<2048;i++)audible+=pcm[i]!=0;CHECK(audible>1900);
 for(int i=0;i<15;i++)mc_sound_mix(&sound,pcm,1024,0,0,0,0);
 CHECK(!sound.voices[0].active);
 mc_sound_play(&sound,"minecraft:block.stone.step",4,100,0,0,1,1,16,-1,1);mc_sound_mix(&sound,pcm,1024,0,0,0,0);for(int i=0;i<2048;i++)CHECK(pcm[i]==0);
 n=0;b(0);CHECK(mc_sound_packet(&sound,&entities,&c,0x63,p,n)==0);for(int i=0;i<16;i++)CHECK(!sound.voices[i].active);
 mc_sound_play(&sound,"minecraft:block.stone.step",4,-2,0,0,1,1,16,-1,1);mc_sound_mix(&sound,pcm,1024,0,0,0,0);for(int i=0;i<1024;i++)CHECK(pcm[2*i]==0);
 McInventory inv;mc_inventory_init(&inv);McInventoryView view;
 for(int type=0;type<25;type++){
  static const int fronts[]={9,18,27,36,45,54,9,3,1,3,5,10,2,3,3,5,1,4,3,27,3,4,3,3,2};
  memset(&view,0,sizeof(view));view.window_id=1;view.window_type=type;view.container_count=fronts[type];view.slot_count=fronts[type]+36;
  McInventoryRect rects[128];unsigned count=mc_inventory_layout(&view,rects);CHECK(count==(unsigned)view.slot_count);
  for(unsigned i=0;i<count;i++){CHECK(rects[i].slot>=0 && rects[i].slot<view.slot_count && rects[i].y+rects[i].size<510);for(unsigned j=0;j<i;j++)CHECK(rects[i].slot!=rects[j].slot);}
 }
 inv.window_id=2;inv.window_type=12;inv.properties[0]=5;
 CHECK(mc_inventory_button_packet(&inv,1,0,100,p)<0);CHECK(mc_inventory_button_packet(&inv,2,1,100,p)<0);
 CHECK(mc_inventory_button_packet(&inv,2,0,100,p)==3 && p[0]==0x0a && p[1]==2 && p[2]==0);CHECK(mc_inventory_button_packet(&inv,2,0,101,p)==0);
 inv.window_type=7;CHECK(mc_inventory_rename_packet(&inv,2,"Vita pick",p,sizeof(p))==11 && p[0]==0x23);
 inv.window_type=8;inv.properties[0]=1;CHECK(mc_inventory_beacon_packet(&inv,2,5,0,p)<0);CHECK(mc_inventory_beacon_packet(&inv,2,1,0,p)==4 && p[0]==0x27);
 inv.properties[0]=4;CHECK(mc_inventory_beacon_packet(&inv,2,1,10,p)==5);
 inv.window_type=18;n=0;vi(2);vi(1);slot(795,2);slot(1,1);b(0);b(0);u32(0);u32(10);u32(1);u32(0);f32(.05f);u32(0);vi(1);vi(0);b(1);b(1);
 CHECK(mc_inventory_packet(&inv,&c,0x2a,p,n)==0 && inv.trade_count==1 && inv.trades[0].sell.item==1);
 CHECK(mc_inventory_packet(&inv,&c,0x2a,p,n-1)<0 && inv.trade_count==1);
 CHECK(mc_inventory_button_packet(&inv,2,0,2000,p)==2 && p[0]==0x26 && p[1]==0);
 mc_inventory_destroy(&inv);
 puts("Social tests passed: chat components/history/wire, sound packets/mixing/spatialization, workstation layouts/trades/buttons");
}
