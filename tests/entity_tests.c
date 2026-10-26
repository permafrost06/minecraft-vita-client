#include "entities.h"
#include "entity_mesh.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t data[8192];static size_t length;static McEntities entities;
static void byte(unsigned x){data[length++]=x;}
static void vi(unsigned x){length+=mc_varint_write(data+length,x);}
static void u16(unsigned x){byte(x>>8);byte(x);}
static void u64(uint64_t x){for(int i=7;i>=0;i--)byte(x>>(i*8));}
static void number(double x){uint64_t bits;memcpy(&bits,&x,8);u64(bits);}
static void real(float x){uint32_t b;memcpy(&b,&x,4);for(int i=3;i>=0;i--)byte(b>>(i*8));}
static void str(const char *s){vi(strlen(s));memcpy(data+length,s,strlen(s));length+=strlen(s);}
static void uuid(unsigned x){for(int i=0;i<16;i++)byte(x+i);}
static void spawn(unsigned id,int type,double x){
 length=0;vi(id);uuid(id);vi(type);number(x);number(64);number(-1.5);byte(0);byte(64);byte(64);vi(0);u16(0);u16(0);u16(0);
}
static int packet(unsigned id,uint64_t now){return mc_entities_packet(&entities,id,data,length,now);}
int main(void){
 mc_entities_init(&entities);spawn(12,18,-1.1);CHECK(packet(1,100)==0 && entities.count==1);
 const McEntity *e=mc_entity_get(&entities,12);CHECK(e && e->type==18 && e->yaw==90 && e->x==-1.1);
 length=0;vi(12);u16(4096);u16(-2048);u16(0);byte(1);CHECK(packet(0x2b,150)==0);
 e=mc_entity_get(&entities,12);CHECK(fabs(e->x-(floor(-1.1*4096)+4096)/4096)<1e-8 && e->y==63.5 && e->grounded);
 double p[3];mc_entity_position(e,175,p);CHECK(p[0]>-1.1 && p[0]<e->x && p[1]==63.75);
 McEntity before=*e;length--;CHECK(packet(0x2b,200)<0 && !memcmp(e,&before,sizeof(before)));
 length=0;vi(12);number(50);number(70);number(-30);byte(128);byte(32);byte(0);CHECK(packet(0x68,200)==0);CHECK(e->x==50 && e->from[0]==50 && e->yaw==180);
 length=0;vi(12);byte(0);vi(0);byte(0x20);byte(16);vi(8);byte(1);byte(9);vi(3);real(8.5);byte(255);CHECK(packet(0x52,250)==0 && e->flags==32 && e->baby && e->health==8.5);
 McEntityVertex vertices[MC_ENTITY_MODEL_VERTICES+1];memset(vertices,0xa5,sizeof(vertices));McEntityVertex sentinel=vertices[MC_ENTITY_MODEL_VERTICES];
 CHECK(mc_entity_model(e,250,vertices,MC_ENTITY_MODEL_VERTICES)==0);
 length=0;vi(12);byte(0);vi(0);byte(0);byte(255);CHECK(packet(0x52,300)==0);
 unsigned n=mc_entity_model(e,300,vertices,MC_ENTITY_MODEL_VERTICES);CHECK(n==216 && !memcmp(&sentinel,&vertices[MC_ENTITY_MODEL_VERTICES],sizeof(sentinel)));
 for(unsigned i=0;i<n;i++)CHECK(isfinite(vertices[i].x) && vertices[i].y>=0 && vertices[i].y<1);
 CHECK(mc_entity_model(e,300,vertices,1)==0);
 spawn(13,54,2);CHECK(packet(1,300)==0);
 length=0;vi(13);byte(8);vi(7);byte(1);vi(261);byte(5);byte(10);u16(0);byte(8);u16(4);memcpy(data+length,"Name",4);length+=4;u16(4);memcpy(data+length,"Test",4);length+=4;byte(0);byte(255);
 CHECK(packet(0x52,350)==0);e=mc_entity_get(&entities,13);CHECK(e->item.item==261 && e->item.count==5 && mc_entity_model(e,350,vertices,MC_ENTITY_MODEL_VERTICES)==36);
 length=0;vi(13);byte(0x80);byte(1);vi(1);byte(1);byte(0);byte(5);byte(0);CHECK(packet(0x55,400)==0 && e->equipment[0].item==1 && e->equipment[5].count==0);
 before=*e;data[2]=0x87;CHECK(packet(0x55,400)<0 && !memcmp(e,&before,sizeof(before)));
 // Names arrive independently of player spawn. Consume all optional info fields.
 length=0;byte(63);vi(1);uuid(14);str("OtherPlayer");vi(1);str("textures");str("value");byte(1);str("signature");byte(1);uuid(50);u64(123);vi(3);byte(1);byte(2);byte(3);vi(2);byte(4);byte(5);vi(1);vi(1);vi(42);byte(1);str("{\"text\":\"Name\"}");CHECK(packet(0x3a,450)==0);
 length=0;vi(14);uuid(14);number(1);number(64);number(3);byte(0);byte(0);CHECK(packet(3,450)==0);e=mc_entity_get(&entities,14);CHECK(!strcmp(mc_entity_name(&entities,e),"OtherPlayer"));
 length=0;vi(1);uuid(14);CHECK(packet(0x39,500)==0 && !strcmp(mc_entity_name(&entities,e),"Player"));
 length=0;vi(3);vi(12);vi(13);vi(999);CHECK(packet(0x3e,500)==0 && entities.count==1 && entities.removed==2);
 // Oversized/malformed destroy lists must not remove any prefix of the list.
 length=0;vi(2);vi(14);byte(128);CHECK(packet(0x3e,550)<0 && mc_entity_get(&entities,14));
 length=0;CHECK(packet(0x41,600)==0 && entities.count==0);
 for(int i=0;i<MC_MAX_ENTITIES+1;i++){spawn(1000+i,18,i);CHECK(packet(1,650)==0);}
 CHECK(entities.count==MC_MAX_ENTITIES && entities.overflow==1);
 mc_entities_init(&entities);spawn(12,18,NAN);CHECK(packet(1,700)<0 && entities.count==0);
 spawn(12,18,0);length--;CHECK(packet(1,700)<0 && entities.count==0);
 puts("Entity tests passed: lifecycle, fixed-point movement, interpolation, teleports, metadata/NBT, equipment, profiles, overflow and model bounds");
}
