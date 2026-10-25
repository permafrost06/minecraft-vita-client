#include "player.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"Failed %s line %d\n",#x,__LINE__);exit(1);}}while(0)
static McWorld world;
static void set(int x,int y,int z,int state){CHECK(mc_world_set(&world,x,y,z,state)==0);}
static void reset_world(void){
    mc_world_destroy(&world);mc_world_init(&world);world.ready=1;world.min_y=0;world.height=128;
    world.chunks[0]=(McChunk){0,0,calloc(128*256,2)};CHECK(world.chunks[0].blocks);
    for(int x=0;x<16;x++)for(int z=0;z<16;z++)set(x,0,z,1);
}
int main(void){
    reset_world();McPlayer p;McInput in={0};
    mc_player_reset(&p,4,8,4,0,0);
    for(int i=0;i<100;i++)mc_player_tick(&p,&world,&in);
    CHECK(fabs(p.y-1)<1e-6 && p.grounded);
    in.jump=1;mc_player_tick(&p,&world,&in);in.jump=0;CHECK(p.y>1.4 && !p.grounded);
    double peak=p.y;for(int i=0;i<30;i++){mc_player_tick(&p,&world,&in);if(p.y>peak)peak=p.y;}
    CHECK(peak>2.2 && peak<2.3 && fabs(p.y-1)<1e-6 && p.grounded);
    for(int y=1;y<=3;y++)for(int x=0;x<16;x++)set(x,y,7,1);
    in.forward=1;for(int i=0;i<40;i++)mc_player_tick(&p,&world,&in);
    CHECK(fabs(p.z-6.7)<1e-6 && fabs(p.y-1)<1e-6);
    mc_player_reset(&p,4,1,4,0,0);in=(McInput){.right=1};mc_player_tick(&p,&world,&in);
    CHECK(p.x<4 && fabs(p.z-4)<1e-6); /* At Java yaw 0, right points west (-X). */
    McCamera c={4,2.62,4,0,0};mc_camera_move(&c,0,1,0,0.05f);CHECK(c.x<4 && c.z==4);
    mc_player_reset(&p,15.6,1,4,0,0);in=(McInput){.right=-1};mc_player_tick(&p,&world,&in);
    CHECK(p.waiting && fabs(p.x-15.6)<1e-6);
    mc_player_reset(&p,4,3,4,0,0);p.flying=1;in=(McInput){.vertical=1};mc_player_tick(&p,&world,&in);
    CHECK(fabs(p.y-3.25)<1e-6 && !p.grounded);
    in.vertical=0;for(int i=0;i<10;i++)mc_player_tick(&p,&world,&in);CHECK(fabs(p.y-3.25)<1e-6);
    p.flying=0;for(int i=0;i<30;i++)mc_player_tick(&p,&world,&in);CHECK(p.grounded && fabs(p.y-1)<1e-6);
    reset_world();
    for(int x=0;x<16;x++)set(x,1,7,11019); /* bottom oak slab, 1.19.4 */
    mc_player_reset(&p,4,1,6,0,0);in=(McInput){.forward=1};
    for(int i=0;i<8;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.z>7.2 && fabs(p.y-1.5)<1e-6 && p.grounded);
    reset_world();
    /* A low ceiling blocks the jump rather than tunneling through it. */
    set(4,3,4,1);mc_player_reset(&p,4.5,1,4.5,0,0);in=(McInput){.jump=1};mc_player_tick(&p,&world,&in);
    CHECK(p.y<=1.2+1e-6);
    mc_world_destroy(&world);puts("Player tests passed: falling, jump height, landing, wall, yaw/strafe, streaming boundary, flight, ceiling");return 0;
}
