#ifndef MC_PLAYER_H
#define MC_PLAYER_H
#include "world.h"
#define MC_PLAYER_EYE 1.62
#define MC_PLAYER_TICK 0.05
typedef struct { int8_t bounds[6]; } McBlockBox;
typedef struct {
    double x,y,z, velocity_y,velocity_x,velocity_z;
    float speed_scale,height,eye;
    int sneaking,sprinting,swimming,in_water,in_lava,eyes_in_water,climbing,scaffolding,horizontal_collision;
    int jump_boost,levitation,slow_falling,depth_strider,dolphins_grace,soul_speed,leather_boots;
    float yaw,pitch;
    int sleeping,jump_held;
    int initialized,grounded,flying,waiting,jump_cooldown;
    uint64_t ticks;
} McPlayer;
typedef struct { float forward,right,vertical,yaw,pitch; int jump,sneak,sprint; } McInput;
const McBlockBox *mc_block_boxes(uint16_t state,unsigned *count);
void mc_player_reset(McPlayer *p,double x,double y,double z,float yaw,float pitch);
/* One fixed 50ms tick. The caller serializes access to the world. */
void mc_player_tick(McPlayer *p,const McWorld *w,const McInput *input);
#endif
