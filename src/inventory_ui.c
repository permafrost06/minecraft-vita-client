#include "inventory_ui.h"
#include <math.h>
#include <stdio.h>
#include "items.h"
#include "enchantment_data.inc"
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
        if((v->window_type==MC_MENU_BLAST_FURNACE || v->window_type==MC_MENU_FURNACE || v->window_type==MC_MENU_SMOKER) && front==3){
            rect(out,&n,0,380,94);rect(out,&n,1,380,174);rect(out,&n,2,540,134);
        }else if(v->window_type==MC_MENU_CRAFTING && front==10){
            rect(out,&n,0,580,134);for(int i=1;i<10;i++)rect(out,&n,i,360+((i-1)%3)*40,74+((i-1)/3)*40);
        }else if(v->window_type==6 && front==9){
            for(int i=0;i<9;i++)rect(out,&n,i,420+(i%3)*40,74+(i/3)*40);
        }else if(v->window_type==MC_MENU_BREWING && front==5){
            rect(out,&n,0,400,174);rect(out,&n,1,460,194);rect(out,&n,2,520,174);
            rect(out,&n,3,460,74);rect(out,&n,4,340,74);
        }else if(v->window_type==MC_MENU_ENCHANTMENT && front==2){
            rect(out,&n,0,300,134);rect(out,&n,1,340,134);
        }else if(v->window_type==MC_MENU_LOOM && front==4){
            rect(out,&n,0,300,94);rect(out,&n,1,340,94);rect(out,&n,2,300,154);rect(out,&n,3,660,134);
        }else if(v->window_type==MC_MENU_BEACON && front==1){rect(out,&n,0,340,214);
        }else if(v->window_type==MC_MENU_LECTERN && front==1){rect(out,&n,0,460,114);
        }else if((v->window_type==MC_MENU_ANVIL || v->window_type==MC_MENU_GRINDSTONE || v->window_type==MC_MENU_MERCHANT || v->window_type==MC_MENU_LEGACY_SMITHING || v->window_type==MC_MENU_SMITHING || v->window_type==MC_MENU_CARTOGRAPHY) && front>=3 && front<=4){
            for(int i=0;i<front-1;i++)rect(out,&n,i,340+i*60,104);
            rect(out,&n,front-1,620,104);
        }else if(v->window_type==MC_MENU_STONECUTTER && front==2){rect(out,&n,0,320,114);rect(out,&n,1,660,114);
        }else if(v->window_type==MC_MENU_HOPPER && front==5){for(int i=0;i<5;i++)rect(out,&n,i,380+i*40,114);
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

int mc_inventory_option_count(const McInventoryView *v){
    switch(v->window_type){case 12:return 3;case 18:return v->trade_count;case 17:return v->slots[0].count && v->slots[1].count && !v->slots[2].count?34:0;case MC_MENU_STONECUTTER:return v->stone_count;case 16:return 4;case 8:return 15;default:return 0;}
}
int mc_inventory_option_button(const McInventoryView *v,int option){return v->window_type==MC_MENU_LECTERN?(option==3?100:option+1):option;}
void mc_inventory_option_label(const McInventoryView *v,int option,char *out,size_t cap){
    if(option<0 || option>=mc_inventory_option_count(v)){snprintf(out,cap,"No options");return;}
    if(v->window_type==MC_MENU_ENCHANTMENT){int clue=v->properties[4+option];const char *name=clue>=0 && (unsigned)clue<sizeof(enchantment_names)/sizeof(enchantment_names[0])?enchantment_names[clue]:"Unknown enchantment";
        if(v->properties[option]>0)snprintf(out,cap,"%s %d...  Requires level %d; costs %d lapis/levels",name,v->properties[7+option],v->properties[option],option+1);
        else snprintf(out,cap,"No enchantment available");
    }
    else if(v->window_type==MC_MENU_MERCHANT){
        const McTrade *t=&v->trades[option];int max=mc_item_def(t->buy.item)->stack_size;
        double adjusted=t->buy.count+fmax(0,floor((double)t->buy.count*t->demand*t->multiplier))+t->special;
        int cost=(int)fmin(max,fmax(1,adjusted));
        snprintf(out,cap,"%d %.20s + %d %.20s -> %d %.20s%s",cost,mc_item_def(t->buy.item)->display,t->second.count,t->second.count?mc_item_def(t->second.item)->display:"",t->sell.count,mc_item_def(t->sell.item)->display,(t->disabled || t->uses>=t->max_uses)?" SOLD OUT":"");
    }else if(v->window_type==MC_MENU_BEACON){static const char *names[]={"Speed","Haste","Resistance","Jump boost","Strength"};snprintf(out,cap,"Beacon: %s%s",names[option%5],option>=10?" + Regeneration (tier 4)":option>=5?" II (tier 4)":"");}
    else if(v->window_type==MC_MENU_LECTERN){static const char *names[]={"Previous page","Next page","Take book","First page"};snprintf(out,cap,"%s (page %d)",names[option],v->properties[0]+1);}
    else if(v->window_type==MC_MENU_STONECUTTER)snprintf(out,cap,"%d %s",v->stone_choices[option].count,mc_item_def(v->stone_choices[option].item)->display);
    else{
        static const char *patterns[]={"Bottom left corner","Bottom right corner","Top left corner","Top right corner","Bottom stripe","Top stripe","Left stripe","Right stripe","Center stripe","Middle stripe","Downright diagonal","Downleft diagonal","Small stripes","Diagonal cross","Straight cross","Bottom triangle","Top triangle","Bottom triangles","Top triangles","Left diagonal","Up right diagonal","Up left diagonal","Right diagonal","Circle","Diamond","Left half","Top half","Right half","Bottom half","Border","Curly border","Gradient","Upward gradient","Bricks"};
        snprintf(out,cap,"%s",patterns[option]);
    }
}
