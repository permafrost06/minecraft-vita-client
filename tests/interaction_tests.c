#include "interaction.h"
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
int main(void){
    mc_mesh_init();
    McWorld w;mc_world_init(&w);w.ready=1;w.height=128;w.min_y=-64;
    w.chunks[0]=(McChunk){0,0,calloc(128*256,2),0};
    w.chunks[80]=(McChunk){-1,-1,calloc(128*256,2),0};
    McTarget t;
    CHECK(mc_world_set(&w,4,2,7,1)==0);
    CHECK(mc_target(&w,4.5,2.5,4.5,0,0,&t));
    CHECK(t.x==4 && t.y==2 && t.z==7 && t.face==2 && fabs(t.distance-2.5)<1e-6 && t.cursor[2]==0);
    CHECK(mc_target(&w,4.5,5,7.5,0,90,&t) && t.face==1);
    CHECK(mc_target(&w,4.5,0,7.5,0,-90,&t) && t.face==0);
    CHECK(mc_target(&w,2,2.5,7.5,-90,0,&t) && t.face==4);
    CHECK(mc_target(&w,7,2.5,7.5,90,0,&t) && t.face==5);
    CHECK(mc_target(&w,4.5,2.5,9,180,0,&t) && t.face==3);
    CHECK(!mc_target(&w,4.5,2.5,1.9,0,0,&t));
    CHECK(mc_target(&w,4.5,2.5,2,0,0,&t) && fabs(t.distance-5)<1e-6);
    CHECK(mc_world_set(&w,4,2,7,11019)==0); /* bottom slab */
    CHECK(!mc_target(&w,4.5,2.75,4.5,0,0,&t));
    CHECK(mc_target(&w,4.5,2.25,4.5,0,0,&t));
    CHECK(mc_target(&w,4.5,5,7.5,0,90,&t) && fabs(t.distance-2.5)<1e-6);
    CHECK(mc_world_set(&w,-4,-2,-7,1)==0);
    CHECK(mc_target(&w,-3.5,-1.5,-3.5,180,0,&t) && t.z==-7 && t.y==-2 && t.face==3);
    CHECK(!mc_target(&w,17,2,4,90,0,&t)); /* unloaded column */
    CHECK(!mc_target(&w,NAN,2,4,0,0,&t));
    McClient c;mc_init(&c);CHECK(mc_interact(&c,&w,0)==-1);
    mc_world_destroy(&w);puts("Interaction targets passed: six faces, reach, slabs, negative coordinates and missing chunks");
}
