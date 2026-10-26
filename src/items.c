#include "items.h"
#include "blocks.h"
#include <stdlib.h>
#include <string.h>
#include "item_data.inc"
static const McItemDef unknown={-1,"unknown","Unknown item",64,-1,0,0xadadad};
unsigned mc_items_count(void){return sizeof(item_defs)/sizeof(item_defs[0]);}
const McItemDef *mc_item_def(int id){return id>=0 && (unsigned)id<mc_items_count()?&item_defs[id]:&unknown;}
static uint32_t rgba(uint32_t rgb,float shade){unsigned r=((rgb>>16)&255)*shade,g=((rgb>>8)&255)*shade,b=(rgb&255)*shade;return 0xff000000u|(b<<16)|(g<<8)|r;}
int mc_item_textures_build(uint32_t *pixels){
    if(mc_items_count()>MC_ITEM_ATLAS_TILES*MC_ITEM_ATLAS_TILES)return -1;
    uint32_t *blocks=malloc(MC_ATLAS_SIZE*MC_ATLAS_SIZE*4);if(!blocks)return -1;
    mc_textures_build(blocks);memset(pixels,0,MC_ITEM_ATLAS_SIZE*MC_ITEM_ATLAS_SIZE*4);
    for(unsigned i=0;i<mc_items_count();i++){
        const McItemDef *d=&item_defs[i];int tx=(i%64)*16,ty=(i/64)*16;
        for(int y=0;y<16;y++)for(int x=0;x<16;x++){
            uint32_t color=0;int on=0,handle=0;
            if(d->state>=0){
                const McBlockDef *b=mc_block_def(d->state);unsigned tile=b->side;
                if(x>=1 && x<=14 && y>=1 && y<=14)color=blocks[((tile/32)*16+y)*MC_ATLAS_SIZE+(tile%32)*16+x];
            }else{
                switch(d->icon){
                case 1:on=abs(x-(15-y))<=1 && y<11;handle=abs(x-(15-y))<=1 && y>=10 && y<=14;on|=y==10 && x>=2 && x<=8;break;
                case 2:on=y>=2 && y<=4 && x>=3 && x<=13;on|=x>=11 && y>=4 && y<=7;handle=abs(x-(15-y))<=1 && y>=5 && y<=14;break;
                case 3:on=x>=7 && x<=13 && y>=2 && y<=8;handle=abs(x-(15-y))<=1 && y>=5 && y<=14;break;
                case 4:on=abs(x-(15-y))<=2 && y>=2 && y<=6;handle=abs(x-(15-y))<=1 && y>=6 && y<=14;break;
                case 5:on=x>=2 && x<=13 && y>=3 && y<=10 && !(x>=5 && x<=10 && y>=7);break;
                case 6:on=y>=2 && y<=13 && x>=2 && x<=13 && !(y<5 && x>=6 && x<=9) && !(y>7 && (x<5 || x>10));break;
                case 7:on=x>=4 && x<=11 && y>=2 && y<=14 && !(x>=7 && x<=8 && y>6);break;
                case 8:on=y>=6 && y<=13 && ((x>=2 && x<=6) || (x>=9 && x<=13));break;
                case 9:on=x>=3 && x<=12 && y>=4 && y<=13 && (y<6 || x==3 || x==12 || y>9);break;
                case 10:on=((x-8)*(x-8)+(y-10)*(y-10)<25) || (x>=6 && x<=9 && y>=2 && y<=8);break;
                case 11:on=x>=3 && x<=12 && y>=2 && y<=13;break;
                default:on=abs(x-8)+abs(y-8)<=6;break;
                }
                if(on)color=rgba(d->color,(x+y)%4==0?.8f:1);
                if(handle)color=rgba(0x9c7244,1);
            }
            pixels[(ty+y)*MC_ITEM_ATLAS_SIZE+tx+x]=color;
        }
    }
    free(blocks);return 0;
}
