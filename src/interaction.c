#include "interaction.h"
#include "blocks.h"
#include "items.h"
#include <math.h>
/* Java 1.19.4 item registry (not block-state IDs): minecraft-data items.json,
   https://github.com/PrismarineJS/minecraft-data/tree/master/data/pc/1.19.4 */
const int mc_build_items[MC_BUILD_BLOCKS]={1,15,14,22,23,109,165,261,44};
const char *const mc_build_names[MC_BUILD_BLOCKS]={"Stone","Dirt","Grass","Cobble","Planks","Oak log","Glass","Bricks","Sand"};
const char *mc_item_label(int item){return item<0?"Empty":mc_item_def(item)->display;}
static int box_hit(const double o[3],const double d[3],const double lo[3],const double hi[3],double *hit,int *face){
    double near=-INFINITY,far=INFINITY;int side=0;
    static const int faces[3]={4,0,2};
    for(int a=0;a<3;a++){
        if(fabs(d[a])<1e-12){if(o[a]<lo[a] || o[a]>hi[a])return 0;continue;}
        double t0=(lo[a]-o[a])/d[a],t1=(hi[a]-o[a])/d[a];
        int f=faces[a];if(t0>t1){double tmp=t0;t0=t1;t1=tmp;f++;}
        if(t0>near){near=t0;side=f;}if(t1<far)far=t1;
        if(near>far)return 0;
    }
    if(near<0 || near>5.0)return 0;
    *hit=near;*face=side;return 1;
}
int mc_target(const McWorld *w,double x,double y,double z,float yaw,float pitch,McTarget *target){
    if(!w->ready || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(yaw) || !isfinite(pitch) ||
       fabs(x)>29999984 || fabs(z)>29999984 || y<-2048 || y>2047)return 0;
    const double rad=0.017453292519943295;
    double o[3]={x,y,z},d[3]={-sin(yaw*rad)*cos(pitch*rad),-sin(pitch*rad),cos(yaw*rad)*cos(pitch*rad)};
    int cell[3]={(int)floor(x),(int)floor(y),(int)floor(z)},step[3];double next[3],delta[3];
    for(int a=0;a<3;a++){
        step[a]=d[a]>0?1:-1;delta[a]=fabs(d[a])<1e-12?INFINITY:fabs(1/d[a]);
        next[a]=isinf(delta[a])?INFINITY:((step[a]>0?cell[a]+1:cell[a])-o[a])/d[a];
    }
    for(int n=0;n<32;n++){
        if(!mc_world_chunk(w,(int)floor(cell[0]/16.0),(int)floor(cell[2]/16.0)))return 0;
        uint16_t state=mc_world_get(w,cell[0],cell[1],cell[2]);
        const McBlockDef *def=mc_block_def(state);
        if(def && def->model!=MM_NONE && def->model!=MM_LIQUID){
            unsigned count;const McBlockBox *boxes=mc_block_render_boxes(state,&count);
            double best=INFINITY;int best_face=0;
            for(unsigned b=0;b<(count?count:1);b++){
                double lo[3],hi[3];
                for(int a=0;a<3;a++){lo[a]=cell[a]+(count?boxes[b].bounds[a]/32.0:0);hi[a]=cell[a]+(count?boxes[b].bounds[a+3]/32.0:1);}
                double t;int face;if(box_hit(o,d,lo,hi,&t,&face) && t<best){best=t;best_face=face;}
            }
            if(isfinite(best)){
                *target=(McTarget){.x=cell[0],.y=cell[1],.z=cell[2],.face=best_face,.distance=best,.state=state};
                for(int a=0;a<3;a++)target->cursor[a]=fmax(0,fmin(1,o[a]+d[a]*best-cell[a]));
                return 1;
            }
        }
        int a=next[0]<next[1]?0:1;if(next[2]<next[a])a=2;
        if(next[a]>5)return 0;
        cell[a]+=step[a];next[a]+=delta[a];
    }
    return 0;
}
int mc_interact(McClient *c,const McWorld *w,int place){
    if(c->gamemode!=1 || !c->position_ready || !c->play || !c->joined)return -1;
    McTarget t;if(!mc_target(w,c->x,c->y+c->eye_height,c->z,c->yaw,c->pitch,&t))return 0;
    return mc_block_action(c,place,t.x,t.y,t.z,t.face,t.cursor);
}
