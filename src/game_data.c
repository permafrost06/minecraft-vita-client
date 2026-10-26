#include "game_data.h"
#include <string.h>
#include "gameplay_data.inc"
static const McDigDef unknown={.hardness=-1,.name="unknown"};
static const McUseDef no_use={0};
const McDigDef *mc_dig_def(uint16_t state){
    unsigned low=0,high=sizeof(dig_defs)/sizeof(dig_defs[0]);
    while(low<high){unsigned mid=(low+high)/2;const McDigDef *d=&dig_defs[mid];if(state<d->low)high=mid;else if(state>d->high)low=mid+1;else return d;}
    return &unknown;
}
const McUseDef *mc_use_def(int item){return item>=0 && (unsigned)item<sizeof(use_defs)/sizeof(use_defs[0])?&use_defs[item]:&no_use;}
float mc_block_tool_speed(uint16_t state,int item,int *harvest){
    const McDigDef *d=mc_dig_def(state);*harvest=!d->harvest_count;float speed=1;
    for(unsigned i=0;i<d->speeds_count;i++)if(tool_speeds[d->speeds_start+i].item==item)speed=tool_speeds[d->speeds_start+i].speed;
    for(unsigned i=0;i<d->harvest_count;i++)if(harvest_tools[d->harvest_start+i]==item)*harvest=1;
    return speed;
}

int mc_block_fluid(uint16_t state){
    const McDigDef *d=mc_dig_def(state);
    if(d->physics&BP_LAVA)return 2;
    if((d->physics&BP_WATER) || (d->waterlogged_step && ((state-d->low)/d->waterlogged_step)%2==0))return 1;
    return 0;
}
int mc_block_fluid_level(uint16_t state){
    const McDigDef *d=mc_dig_def(state);
    return !strcmp(d->name,"water") || !strcmp(d->name,"lava")?state-d->low:0;
}
