#include "player.h"
#include "render.h"
#include "game_data.h"
#include <string.h>
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
static unsigned state(const char *name){
    for(unsigned i=0;i<65535;i++)if(!strcmp(mc_dig_def(i)->name,name))return i;
    fprintf(stderr,"Missing block %s\n",name);exit(1);
}
static void extended(void){
    McPlayer p;McInput in={.forward=1};reset_world();mc_player_reset(&p,4,1,2,0,0);
    for(int i=0;i<20;i++)mc_player_tick(&p,&world,&in);
    double walk=p.z-2;CHECK(walk>4 && walk<4.4);
    mc_player_reset(&p,4,1,2,0,0);in.sprint=1;
    for(int i=0;i<20;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.sprinting && p.z-2>walk*1.29 && p.z-2<walk*1.31);
    /* Crouching fits under a bottom slab and keeps feet on a one-block ledge. */
    reset_world();for(int z=3;z<9;z++)set(4,2,z,11018);
    mc_player_reset(&p,4.5,1,2,0,0);in=(McInput){.forward=1,.sneak=1};
    for(int i=0;i<55;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.height==1.5f && fabs(p.eye-1.27)<.001 && p.z>4);
    in.sneak=0;in.forward=0;mc_player_tick(&p,&world,&in);CHECK(p.height==1.5f);
    reset_world();for(int x=0;x<16;x++)for(int z=5;z<16;z++)set(x,0,z,0);
    mc_player_reset(&p,4.5,1,4,0,0);in=(McInput){.forward=1,.sneak=1};
    for(int i=0;i<80;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.z<5.3 && p.y==1 && p.grounded);
    /* Source fluids buoy jumping players; sprint swimming changes the pose. */
    for(int kind=0;kind<2;kind++){
        reset_world();unsigned fluid=state(kind?"lava":"water");
        for(int y=1;y<=5;y++)for(int x=2;x<=6;x++)for(int z=2;z<=6;z++)set(x,y,z,fluid);
        mc_player_reset(&p,4,2,4,0,0);in=(McInput){.jump=1};
        for(int i=0;i<10;i++)mc_player_tick(&p,&world,&in);
        CHECK(p.y>2.3 && (kind?p.in_lava:p.in_water));
        if(!kind){in=(McInput){.forward=1,.sprint=1};mc_player_tick(&p,&world,&in);CHECK(p.swimming && p.height<1 && p.eye<.5);}
    }
    CHECK(mc_block_fluid(11020)==0 && mc_block_fluid(11019)==1); /* dry/waterlogged bottom slab */
    reset_world();for(int y=1;y<8;y++)set(4,y,4,state("ladder"));
    mc_player_reset(&p,4.5,1,4.5,0,0);in=(McInput){.jump=1};
    for(int i=0;i<15;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.climbing && p.y>3.5);
    /* Ice preserves horizontal momentum after input is released. */
    reset_world();for(int x=0;x<16;x++)for(int z=0;z<16;z++)set(x,0,z,state("ice"));
    mc_player_reset(&p,4,1,4,0,0);p.velocity_z=.3;in=(McInput){0};
    for(int i=0;i<5;i++)mc_player_tick(&p,&world,&in);CHECK(p.velocity_z>.16);
    reset_world();set(4,0,4,state("slime_block"));mc_player_reset(&p,4.5,5,4.5,0,0);
    int bounced=0;for(int i=0;i<25;i++){mc_player_tick(&p,&world,&in);if(p.velocity_y>.5)bounced=1;}CHECK(bounced);
    reset_world();mc_player_reset(&p,4,10,4,0,0);p.slow_falling=1;
    for(int i=0;i<10;i++)mc_player_tick(&p,&world,&in);CHECK(p.y>9 && p.velocity_y>-.1);
    p.levitation=2;for(int i=0;i<15;i++)mc_player_tick(&p,&world,&in);CHECK(p.velocity_y>.08);
    /* Border uses the player's width even in creative flight. */
    reset_world();world.border_x=world.border_z=4;world.border_old=world.border_new=4;
    mc_player_reset(&p,4,2,4,0,0);p.flying=1;in=(McInput){.forward=1};
    for(int i=0;i<30;i++)mc_player_tick(&p,&world,&in);CHECK(fabs(p.z-5.7)<1e-6);
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
    mc_player_reset(&p,15.6,1,4,0,0);in=(McInput){.right=-1};for(int i=0;i<5;i++)mc_player_tick(&p,&world,&in);
    CHECK(p.waiting && p.x<=15.7);
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
    extended();mc_world_destroy(&world);puts("Player tests passed: falling, jump height, landing, wall, yaw/strafe, streaming boundary, flight, ceiling");return 0;
}
