#include "inventory_render.h"
#include "gl_render.h"
#include "items.h"
#include <string.h>
static const char *label(McSlot slot){return slot.count?mc_item_def(slot.item)->display:"Empty";}
static void count(float x,float y,int value){if(value>1)mc_gl_textf(x,y,1.25f,0xffffffff,"%d",value);}
void mc_inventory_draw(const McInventoryView *v,int selected,int catalog,int catalog_item,int hotbar,const McSlot hotbar_slots[9],int creative,int option_focus,int option){
    mc_gl_rect(0,0,960,544,0xb0101820);mc_gl_rect(100,16,760,516,0xf01b2530);
    mc_gl_rect(100,16,760,3,0xff8ab762);
    if(catalog){
        int first=(catalog_item/45)*45;unsigned total=mc_items_count();
        mc_gl_textf(128,32,2,0xffffffff,"CREATIVE  %d/%u",catalog_item/45+1,(total-1)/45+1);
        for(int i=0;i<45 && (unsigned)(first+i)<total;i++)mc_gl_rect(210+(i%9)*60,82+(i/9)*68,54,54,first+i==catalog_item?0xff64854b:0xff34414d);
        for(int i=0;i<9;i++)mc_gl_rect(210+i*60,438,54,50,i==hotbar?0xff9b8459:0xff34414d);
        for(int i=0;i<45 && (unsigned)(first+i)<total;i++)mc_gl_item_icon(first+i,215+(i%9)*60,87+(i/9)*68,44);
        for(int i=0;i<9;i++)if(hotbar_slots[i].count)mc_gl_item_icon(hotbar_slots[i].item,216+i*60,442,38);
        if(first==0)mc_gl_text(230,100,2,0xffee9070,"X");
        mc_gl_textf(210,414,1.5f,0xffffffff,"%.47s",catalog_item?mc_item_def(catalog_item)->display:"Clear selected hotbar slot");
        mc_gl_textf(210,492,1.25f,0xffced8e0,"Destination: hotbar %d",hotbar+1);
        mc_gl_text(126,516,1,0xffd0dae0,"D-pad browse  L/R page  X assign  TRIANGLE slot  SQUARE bag  O close");
        return;
    }
    mc_gl_text(128,32,2,0xffffffff,v->title);
    McSlot current=selected>=0 && selected<v->slot_count?v->slots[selected]:(McSlot){-1,0};
    mc_gl_textf(410,36,1.25f,0xffd8e4ed,"%.41s",label(current));
    McInventoryRect rects[MC_INVENTORY_SLOTS];unsigned n=mc_inventory_layout(v,rects);
    for(unsigned i=0;i<n;i++)mc_gl_rect(rects[i].x,rects[i].y,38,38,rects[i].slot==selected?0xff66894e:0xff34414d);
    if(v->carried.count)mc_gl_rect(760,78,48,48,0xff536474);
    if(v->window_type==9 || v->window_type==13 || v->window_type==21){
        float cooking=v->properties[3]>0?(float)v->properties[2]/v->properties[3]:0;
        if(cooking<0)cooking=0;
        if(cooking>1)cooking=1;
        mc_gl_rect(438,146,76,12,0xff34414d);mc_gl_rect(438,146,76*cooking,12,0xffd0a85e);
    }
    for(unsigned i=0;i<n;i++){McSlot item=v->slots[rects[i].slot];if(item.count)mc_gl_item_icon(item.item,rects[i].x+4,rects[i].y+4,30);}
    if(v->carried.count)mc_gl_item_icon(v->carried.item,764,82,40);
    for(unsigned i=0;i<n;i++){McSlot item=v->slots[rects[i].slot];count(rects[i].x+20,rects[i].y+25,item.count);}
    if(v->carried.count){mc_gl_text(750,60,1,0xffd0dae0,"Carrying");count(784,114,v->carried.count);}
    if(!v->window_id){mc_gl_text(390,74,1.25f,0xffd0dae0,"Crafting");mc_gl_text(200,54,1,0xffd0dae0,"Armor");mc_gl_text(700,174,1,0xffd0dae0,"Offhand");}
    if(v->window_type==7){mc_gl_textf(300,174,1.25f,0xffe5c97e,"Repair cost: %d levels",v->properties[0]);mc_gl_text(300,196,1.25f,0xffced8e0,"SQUARE rename item");}
    if(v->window_type==10)mc_gl_textf(330,244,1.25f,0xffe5c97e,"Brew time: %d   Fuel: %d",v->properties[0],v->properties[1]);
    int options=mc_inventory_option_count(v);
    if(options){
        char description[192];mc_inventory_option_label(v,option,description,sizeof(description));
        mc_gl_rect(150,242,660,40,option_focus?0xff526b40:0xff293846);
        mc_gl_textf(160,246,1,0xffe8eddf,"%d/%d  %.97s",option+1,options,description);
        mc_gl_text(160,264,1,0xffced8e0,option_focus?"D-pad choose  X send selection  SQUARE return to slots":"SQUARE select options");
    }
    if(v->pending)mc_gl_text(710,490,1,0xffe4c77d,"Waiting...");
    mc_gl_text(126,516,1,0xffd0dae0,creative?"D-pad move  X pick/place  TRIANGLE split  L transfer  R drop  SQUARE catalog  O close":"D-pad move  X pick/place  TRIANGLE split  L transfer  R drop  O close");
}
