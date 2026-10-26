#include "world.h"
#include "text.h"
#include <string.h>
int mc_block_entity_sign_line(const McBlockEntity *e,unsigned line,char *out,size_t capacity){
    if(!capacity)return -1;
    out[0]=0;if(line>=4)return -1;
    McNbt field;char key[]="Text1";key[4]+=line;
    if(mc_nbt_find(mc_block_entity_nbt(e),key,&field)<0)return -1;
    char json[MC_BLOCK_ENTITY_NBT];if(mc_nbt_string(field,json,sizeof(json))<0)return -1;
    return mc_text_plain((const uint8_t *)json,strlen(json),out,capacity);
}
