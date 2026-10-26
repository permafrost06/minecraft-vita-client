#include "inventory_ui.h"
#include <math.h>
static void rect(McInventoryRect *out,unsigned *n,int slot,float x,float y){out[(*n)++]=(McInventoryRect){slot,x,y,40};}
unsigned mc_inventory_layout(const McInventoryView *v,McInventoryRect out[MC_INVENTORY_SLOTS]){
    unsigned n=0;
    if(!v->window_id){
        rect(out,&n,0,570,114);
        for(int i=1;i<=4;i++)rect(out,&n,i,390+((i-1)%2)*40,94+((i-1)/2)*40);
        for(int i=5;i<=8;i++)rect(out,&n,i,220,74+(i-5)*40);
        rect(out,&n,45,700,194);
        for(int i=9;i<36;i++)rect(out,&n,i,300+((i-9)%9)*40,292+((i-9)/9)*40);
        for(int i=36;i<45;i++)rect(out,&n,i,300+(i-36)*40,432);
    }else{
        int front=v->container_count;float player_y=292;
        if((v->window_type==9 || v->window_type==13 || v->window_type==21) && front==3){
            rect(out,&n,0,380,94);rect(out,&n,1,380,174);rect(out,&n,2,540,134);
        }else if(v->window_type==11 && front==10){
            rect(out,&n,0,580,134);for(int i=1;i<10;i++)rect(out,&n,i,360+((i-1)%3)*40,74+((i-1)/3)*40);
        }else{
            for(int i=0;i<front && i<54;i++)rect(out,&n,i,300+(i%9)*40,66+(i/9)*40);
            float below=86+((front+8)/9)*40;if(below>player_y)player_y=below;
        }
        if(v->slot_count-front==36){
            for(int i=0;i<27;i++)rect(out,&n,front+i,300+(i%9)*40,player_y+(i/9)*40);
            for(int i=0;i<9;i++)rect(out,&n,front+27+i,300+i*40,player_y+140);
        }
    }
    return n;
}
int mc_inventory_navigate(const McInventoryRect *r,unsigned count,int slot,int dx,int dy){
    int current=-1;for(unsigned i=0;i<count;i++)if(r[i].slot==slot)current=i;
    if(current<0)return count?r[0].slot:0;
    float best=1e30f;int result=slot;
    for(unsigned i=0;i<count;i++){
        float x=r[i].x-r[current].x,y=r[i].y-r[current].y,forward=x*dx+y*dy,across=x*dy-y*dx;
        if(forward<=0)continue;
        float score=forward*forward+across*across*5;
        if(score<best){best=score;result=r[i].slot;}
    }
    return result;
}
