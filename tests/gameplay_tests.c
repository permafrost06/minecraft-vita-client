#include "gameplay.h"
#include "mesh.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static McWorld world;static McEntities entities;static McInventory inventory;
static int has(const McGameAction *a,unsigned n,int type,int status){for(unsigned i=0;i<n;i++)if(a[i].type==type && a[i].status==status)return 1;return 0;}
int main(void){
 mc_mesh_init();mc_world_init(&world);mc_inventory_init(&inventory);mc_entities_init(&entities);
 world.ready=1;world.height=64;world.chunks[0]=(McChunk){0,0,calloc(64*256,2),1};CHECK(world.chunks[0].blocks);
 for(int z=0;z<16;z++)for(int x=0;x<16;x++)mc_world_set(&world,x,0,z,1);
 mc_world_set(&world,4,1,6,10);
 McClient c;mc_init(&c);c.play=c.joined=c.position_ready=1;c.entity_id=1;c.x=4.5;c.y=1;c.z=4.5;c.pitch=45;
 McPlayer p;mc_player_reset(&p,c.x,c.y,c.z,0,45);p.grounded=1;
 McItem bare={.stack={-1,0}},wood={.stack={775,1}},diamond={.stack={795,1}};
 static const uint8_t enchanted_wire[]={1,155,6,1,10,0,0,9,0,12,69,110,99,104,97,110,116,109,101,110,116,115,10,0,0,0,1,8,0,2,105,100,0,20,109,105,110,101,99,114,97,102,116,58,101,102,102,105,99,105,101,110,99,121,2,0,3,108,118,108,0,5,0,0};
 static const uint8_t adventure_wire[]={1,1,1,10,0,0,9,0,10,67,97,110,68,101,115,116,114,111,121,8,0,0,0,1,0,14,109,105,110,101,99,114,97,102,116,58,100,105,114,116,0};
 McItem enchanted={.stack={795,1},.wire=(uint8_t *)enchanted_wire,.size=sizeof(enchanted_wire)},adventure={.stack={1,1},.wire=(uint8_t *)adventure_wire,.size=sizeof(adventure_wire)};
 CHECK(mc_item_enchantment(&enchanted,"efficiency")==5 && mc_item_enchantment(&enchanted,"silk_touch")==0);
 CHECK(mc_break_delta(1,&enchanted,1,0,0,-1,-1)>mc_break_delta(1,&diamond,1,0,0,-1,-1)*4);
 CHECK(mc_item_can_destroy(&adventure,"dirt") && !mc_item_can_destroy(&adventure,"stone"));
 CHECK(fabsf(mc_break_delta(10,&bare,1,0,0,-1,-1)-1.0f/15)<1e-6);
 CHECK(mc_break_delta(1,&wood,1,0,0,-1,-1)>mc_break_delta(1,&bare,1,0,0,-1,-1));
 CHECK(mc_break_delta(1,&diamond,1,0,0,-1,-1)>mc_break_delta(1,&wood,1,0,0,-1,-1));
 CHECK(mc_break_delta(79,&diamond,1,0,0,-1,-1)==0);
 CHECK(fabsf(mc_break_delta(1,&wood,0,0,0,-1,-1)*5-mc_break_delta(1,&wood,1,0,0,-1,-1))<1e-6);
 CHECK(fabsf(mc_break_delta(1,&wood,1,1,0,-1,-1)*5-mc_break_delta(1,&wood,1,0,0,-1,-1))<1e-6);
 CHECK(mc_break_delta(1,&wood,1,1,1,-1,-1)==mc_break_delta(1,&wood,1,0,0,-1,-1));
 CHECK(mc_break_delta(1,&wood,1,0,0,1,-1)>mc_break_delta(1,&wood,1,0,0,-1,-1));
 CHECK(mc_break_delta(1,&wood,1,0,0,-1,2)<mc_break_delta(1,&wood,1,0,0,-1,-1)/100);
 McGameplay g;mc_gameplay_reset(&g);McGameAction actions[MC_GAME_ACTIONS];
 unsigned n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,0,actions);CHECK(has(actions,n,MC_ACTION_DIG,0) && g.mining && !g.finished);
 int finishes=0;for(int i=1;i<24;i++){n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,i*50,actions);finishes+=has(actions,n,MC_ACTION_DIG,2);}
 CHECK(finishes==1 && g.finished && g.progress==1 && mc_world_get(&world,4,1,6)==10);
 mc_gameplay_reset(&g);mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,0,actions);
 n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,0,0,50,actions);CHECK(has(actions,n,MC_ACTION_DIG,1) && !g.mining);
 mc_gameplay_reset(&g);mc_inventory_plain_hotbar(&inventory,0,755,64);c.food=10;
 n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,0,1,100,actions);CHECK(has(actions,n,MC_ACTION_USE_AIR,0) && g.using_item);
 n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,0,0,150,actions);CHECK(has(actions,n,MC_ACTION_RELEASE,0) && !g.using_item);
 mc_gameplay_reset(&g);c.food=20;n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,0,1,200,actions);CHECK(!has(actions,n,MC_ACTION_USE_AIR,0));
 mc_inventory_plain_hotbar(&inventory,0,0,0);mc_gameplay_reset(&g);n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,0,1,300,actions);CHECK(has(actions,n,MC_ACTION_USE_BLOCK,0));
 entities.entries[0]=(McEntity){.active=1,.id=42,.type=MC_ENTITY_PLAYER,.x=4.5,.y=1,.z=6};entities.count=1;
 mc_gameplay_reset(&g);n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,400,actions);CHECK(has(actions,n,MC_ACTION_ATTACK,0) && !has(actions,n,MC_ACTION_DIG,0));
 n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,450,actions);CHECK(!has(actions,n,MC_ACTION_ATTACK,0));
 mc_world_set(&world,4,2,5,1);mc_gameplay_reset(&g);n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,500,actions);CHECK(!has(actions,n,MC_ACTION_ATTACK,0) && has(actions,n,MC_ACTION_DIG,0));
 mc_world_set(&world,4,2,5,0);entities.entries[0].health_known=1;entities.entries[0].health=0;mc_gameplay_reset(&g);n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,0,550,actions);CHECK(!has(actions,n,MC_ACTION_ATTACK,0));
 c.gamemode=3;n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,1,600,actions);CHECK(!has(actions,n,MC_ACTION_ATTACK,0) && !has(actions,n,MC_ACTION_USE_BLOCK,0));
 c.gamemode=0;c.health_ready=1;c.health=0;n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,1,700,actions);CHECK(n==0 && !g.mining);
 mc_player_reset(&p,4.5,1,4.5,0,0);p.velocity_x=.4;p.velocity_y=.3;McInput input={0};mc_player_tick(&p,&world,&input);CHECK(p.x>4.8 && p.y>1.2 && !p.grounded);
 for(int i=0;i<80;i++)mc_player_tick(&p,&world,&input);CHECK(p.grounded && p.y==1 && p.velocity_x<.001);
 c.pose=2;c.health=20;input=(McInput){.forward=1,.jump=0};mc_gameplay_movement(&c,&inventory,&p,&input,1000);double sleep_x=p.x,sleep_y=p.y;mc_player_tick(&p,&world,&input);CHECK(p.sleeping && p.x==sleep_x && p.y==sleep_y && p.eye<.3f);
 n=mc_gameplay_tick(&g,&c,&world,&inventory,&entities,&p,1,1,1,1000,actions);CHECK(n==0);
 c.pose=0;mc_gameplay_movement(&c,&inventory,&p,&input,1050);CHECK(!p.sleeping);
 mc_inventory_destroy(&inventory);mc_world_destroy(&world);puts("Gameplay tests passed: tools/hardness, penalties/effects, timed mining/cancel, server-owned terrain, item use/release, entity targeting/occlusion, cooldown, spectator/death guards and knockback");
}
