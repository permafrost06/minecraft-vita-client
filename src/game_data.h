#ifndef MC_GAME_DATA_H
#define MC_GAME_DATA_H
#include <stdint.h>
typedef struct {uint16_t item;float speed;} McToolSpeed;
typedef struct {
    uint16_t low,high;float hardness;
    uint16_t speeds_start,speeds_count,harvest_start,harvest_count;
    uint8_t diggable,interactive;uint16_t physics,waterlogged_step;const char *name;
} McDigDef;
typedef struct {int ticks,food,always_eat;} McUseDef;
const McDigDef *mc_dig_def(uint16_t state);
const McUseDef *mc_use_def(int item);
int mc_block_fluid(uint16_t state);
int mc_block_fluid_level(uint16_t state);
float mc_block_tool_speed(uint16_t state,int item,int *harvest);
enum { BP_WEB=1,BP_SLIME=2,BP_HONEY=4,BP_SOUL_SAND=8,BP_ICE=16,BP_BLUE_ICE=32,BP_CLIMB=64,BP_SCAFFOLD=128,BP_BUBBLE=256,BP_BED=512,BP_SOUL=1024,BP_POWDER=2048,BP_WATER=4096,BP_LAVA=8192 };
#endif
