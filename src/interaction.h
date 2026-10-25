#ifndef MC_INTERACTION_H
#define MC_INTERACTION_H
#include "player.h"
typedef struct {
    int x,y,z,face;
    float cursor[3];
    double distance;
    uint16_t state;
} McTarget;
/* Java faces: down/up/north/south/west/east. Stops at unloaded columns.
   Liquids are ignored; partial collision shapes are targeted precisely.
   Initialize material lookup with mc_mesh_init (as the renderers do) first. */
int mc_target(const McWorld *w,double x,double y,double z,float yaw,float pitch,McTarget *target);
/* Uses the last position/look sent to the server. No speculative world edits. */
int mc_interact(McClient *c,const McWorld *w,int place);
#define MC_BUILD_BLOCKS 9
extern const int mc_build_items[MC_BUILD_BLOCKS];
extern const char *const mc_build_names[MC_BUILD_BLOCKS];
const char *mc_item_label(int item);
#endif
