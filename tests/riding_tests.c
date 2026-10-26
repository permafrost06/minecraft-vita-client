#include "riding.h"
#include "net.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int peer;
static void connect_client(McClient *c){int pair[2];CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);mc_init(c);c->fd=pair[0];peer=pair[1];c->play=c->joined=c->position_ready=1;}
static void receive(uint8_t *data,size_t size){CHECK(recv(peer,data,size,MSG_WAITALL)==(ssize_t)size);}
static void packet(const uint8_t *data,size_t size){uint8_t prefix[5];size_t n=mc_varint_write(prefix,size);CHECK(send(peer,prefix,n,0)==(ssize_t)n);CHECK(send(peer,data,size,0)==(ssize_t)size);}
static void number(uint8_t *out,double value){uint64_t bits;memcpy(&bits,&value,8);for(int i=0;i<8;i++)out[i]=bits>>(56-i*8);}
int main(void){
 CHECK(net_init()==0);McClient c;connect_client(&c);uint8_t wire[128];
 CHECK(mc_rider_input(&c,-1,.5,1,1,90,-30)==0);receive(wire,22);
 CHECK(wire[0]==10 && wire[1]==0x16 && wire[11]==10 && wire[12]==0x1f && wire[21]==3);
 McReader r={wire+13,8,0,0};CHECK(mc_f32(&r)==-1 && mc_f32(&r)==.5);
 CHECK(mc_rider_input(&c,NAN,0,0,0,0,0)<0 && mc_rider_input(&c,0,2,0,0,0,0)<0);
 CHECK(mc_vehicle_move(&c,1.5,-60,4,0,0)==0);receive(wire,34);CHECK(wire[0]==33 && wire[1]==0x18);r=(McReader){wire+2,24,0,0};CHECK(mc_f64(&r)==1.5 && mc_f64(&r)==-60 && mc_f64(&r)==4);
 CHECK(mc_vehicle_move(&c,NAN,0,0,0,0)<0 && mc_vehicle_move(&c,0,0,0,0,91)<0);
 CHECK(mc_boat_paddles(&c,1,0)==0);receive(wire,4);CHECK(wire[0]==3 && wire[1]==0x19 && wire[2]==1 && wire[3]==0);
 uint8_t correction[33]={0x2e};number(correction+1,-3.5);number(correction+9,70);number(correction+17,4.25);packet(correction,sizeof(correction));CHECK(mc_step(&c)>=0 && c.vehicle_updates==1 && c.vehicle_position[0]==-3.5 && c.vehicle_position[1]==70);
 packet(correction,sizeof(correction)-1);CHECK(mc_step(&c)<0 && c.vehicle_updates==1 && c.vehicle_position[0]==-3.5);mc_close(&c);close(peer);
 /* Waking is a rising edge; a held jump cannot emit requests every tick. */
 connect_client(&c);McInventory inv;mc_inventory_init(&inv);McPlayer p;mc_player_reset(&p,0,64,0,0,0);McInput in={.jump=1};c.pose=2;
 mc_gameplay_movement(&c,&inv,&p,&in,0);receive(wire,9);CHECK(wire[1]==0x1e && wire[7]==2);
 mc_gameplay_movement(&c,&inv,&p,&in,50);CHECK(!net_ready(peer,1));
 /* Unknown mounted entities pause walking until spawn, but still allow dismount. */
 static McEntities entities;static McWorld world;mc_entities_init(&entities);mc_world_init(&world);entities.local_vehicle=42;in=(McInput){.sneak=1};
 CHECK(mc_riding_tick(&c,&entities,&p,&world,&in)==1 && p.waiting && p.vehicle_id==42);receive(wire,22);CHECK(wire[21]==2);
 entities.local_vehicle=-1;CHECK(mc_riding_tick(&c,&entities,&p,&world,&in)==0 && p.vehicle_id==-1);
 mc_world_destroy(&world);
 mc_inventory_destroy(&inv);mc_close(&c);close(peer);net_term();puts("Riding wire checks passed");return 0;
}
