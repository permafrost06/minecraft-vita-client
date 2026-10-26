#include "inventory.h"
#include "items.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static McItem empty_item(void){return (McItem){.stack={-1,0}};}
void mc_inventory_init(McInventory *inv){
    memset(inv,0,sizeof(*inv));inv->window_type=-1;inv->slot_count=46;
    for(int i=0;i<46;i++)inv->player[i]=empty_item();
    for(int i=0;i<MC_INVENTORY_SLOTS;i++)inv->container[i]=empty_item();
    inv->carried=empty_item();snprintf(inv->title,sizeof(inv->title),"Inventory");
}
static void release(McInventory *inv,McItem *item){free(item->wire);inv->bytes-=item->size;*item=empty_item();}
void mc_inventory_destroy(McInventory *inv){
    for(int i=0;i<46;i++)release(inv,&inv->player[i]);
    for(int i=0;i<MC_INVENTORY_SLOTS;i++)release(inv,&inv->container[i]);
    release(inv,&inv->carried);mc_inventory_init(inv);
}
static void close_window(McInventory *inv){
    for(int i=0;i<MC_INVENTORY_SLOTS;i++)release(inv,&inv->container[i]);
    release(inv,&inv->carried);inv->window_id=0;inv->window_type=-1;inv->container_count=0;inv->slot_count=46;inv->pending_until=0;
    memset(inv->properties,0,sizeof(inv->properties));snprintf(inv->title,sizeof(inv->title),"Inventory");inv->revision++;
}
static McItem *slot_at(McInventory *inv,int slot){
    if(slot<0 || slot>=inv->slot_count)return NULL;
    if(!inv->window_id)return slot<46?&inv->player[slot]:NULL;
    if(slot<inv->container_count)return &inv->container[slot];
    return slot-inv->container_count<36?&inv->player[9+slot-inv->container_count]:NULL;
}
const McItem *mc_inventory_item(const McInventory *inv,int slot){
    if(slot<0 || slot>=inv->slot_count)return NULL;
    if(!inv->window_id)return slot<46?&inv->player[slot]:NULL;
    if(slot<inv->container_count)return &inv->container[slot];
    return slot-inv->container_count<36?&inv->player[9+slot-inv->container_count]:NULL;
}
static void hotbar(const McInventory *inv,McClient *c){for(int i=0;i<9;i++)c->hotbar[i]=inv->player[36+i].stack;}
static McItem read_item(McReader *r,size_t *allocated){
    size_t start=r->pos;McItem item={.stack=mc_read_slot(r)};
    if(r->error || !item.stack.count)return item;
    size_t size=r->pos-start;
    if(size>MC_ITEM_WIRE_MAX || size>MC_INVENTORY_BYTES_MAX-*allocated){r->error=1;return item;}
    item.wire=malloc(size);if(!item.wire){r->error=1;return item;}
    item.size=size;memcpy(item.wire,r->p+start,size);*allocated+=size;return item;
}
static int bad(McInventory *inv){snprintf(inv->error,sizeof(inv->error),"Malformed or oversized inventory packet");return -1;}
static const char *window_name(int type){
    static const char *names[]={"Chest","Chest","Chest","Chest","Chest","Chest","Dispenser","Anvil","Beacon","Blast furnace","Brewing stand","Crafting table","Enchanting table","Furnace","Grindstone","Hopper","Lectern","Loom","Trading","Shulker box","Smithing table","Smoker","Cartography table","Stonecutter"};
    return type>=0 && (unsigned)type<sizeof(names)/sizeof(names[0])?names[type]:"Container";
}
static void title(McInventory *inv,const uint8_t *p,size_t n){
    /* Use the menu's English name for translation components. Preserve plain
       custom titles while consuming the complete bounded component on wire. */
    snprintf(inv->title,sizeof(inv->title),"%s",window_name(inv->window_type));
    size_t at=0;
    if(n && p[0]=='"')at=1;
    else for(size_t i=0;i+6<n;i++)if(!memcmp(p+i,"\"text\"",6)){
        at=i+6;while(at<n && p[at]!=':')at++;if(at<n)at++;
        while(at<n && (p[at]==' ' || p[at]=='\t'))at++;
        if(at<n && p[at]=='"')at++;else at=0;break;
    }
    if(!at)return;
    size_t used=0;
    while(at<n && p[at]!='"' && used<sizeof(inv->title)-1){
        unsigned c=p[at++];if(c=='\\' && at<n)c=p[at++];inv->title[used++]=c<32?' ':c;
    }
    inv->title[used]=0;
}
int mc_inventory_packet(McInventory *inv,McClient *c,uint32_t id,const uint8_t *p,size_t n){
    McReader r={p,n,0,0};
    if(id==0x28){mc_inventory_destroy(inv);return 0;}
    if(id==0x41){close_window(inv);return 0;}
    if(id==0x30){
        uint32_t window=mc_varint_read(&r),type=mc_varint_read(&r),len=mc_varint_read(&r);
        if(r.error || !window || window>127 || type>32767 || len>32767 || len!=n-r.pos)return bad(inv);
        close_window(inv);inv->window_id=window;inv->window_type=type;inv->slot_count=0;title(inv,p+r.pos,len);return 0;
    }
    if(id==0x11){unsigned window=mc_u8(&r);if(r.error || r.pos!=n)return bad(inv);if((int)window==inv->window_id)close_window(inv);return 0;}
    if(id==0x13){
        unsigned window=mc_u8(&r);int property=(int16_t)mc_u16(&r),value=(int16_t)mc_u16(&r);
        if(r.error || r.pos!=n)return bad(inv);
        if((int)window==inv->window_id && property>=0 && property<16)inv->properties[property]=value;
        return 0;
    }
    if(id!=0x12 && id!=0x14)return 0;
    int window=(int8_t)mc_u8(&r);uint32_t state=mc_varint_read(&r);if(state>INT32_MAX)return bad(inv);
    if(id==0x14){
        int slot=(int16_t)mc_u16(&r);size_t allocated=0;McItem value=read_item(&r,&allocated);McItem *dst=NULL;
        if(window==-1 && slot==-1)dst=&inv->carried;
        else if(window==-2){
            int index=slot<9?36+slot:slot<36?slot:slot<40?44-slot:slot==40?45:-1;
            if(slot>=0 && index>=0 && index<46)dst=&inv->player[index];
        }else if(window==0 && slot>=0 && slot<46)dst=&inv->player[slot];
        else if(window==inv->window_id)dst=slot_at(inv,slot);
        if(r.error || r.pos!=n || (dst && inv->bytes-dst->size+allocated>MC_INVENTORY_BYTES_MAX)){free(value.wire);return bad(inv);}
        if(dst){release(inv,dst);*dst=value;inv->bytes+=value.size;inv->revision++;inv->pending_until=0;}
        else free(value.wire);
        if(window==0)inv->player_state_id=state;
        if(window==inv->window_id)inv->state_id=state;
    }else{
        uint32_t count=mc_varint_read(&r);if(count>MC_INVENTORY_SLOTS || (window==0 && count!=46))return bad(inv);
        McItem slots[MC_INVENTORY_SLOTS];for(int i=0;i<MC_INVENTORY_SLOTS;i++)slots[i]=empty_item();
        size_t allocated=0;for(uint32_t i=0;i<count && !r.error;i++)slots[i]=read_item(&r,&allocated);
        McItem carried=read_item(&r,&allocated);int accept=window==0 || window==inv->window_id;
        size_t retained=inv->bytes;
        if(accept){
            retained-=inv->carried.size;
            if(window==0)for(int i=0;i<46;i++)retained-=inv->player[i].size;
            else{
                for(int i=0;i<MC_INVENTORY_SLOTS;i++)retained-=inv->container[i].size;
                if(count>=36)for(int i=9;i<45;i++)retained-=inv->player[i].size;
            }
        }
        if(r.error || r.pos!=n || (accept && retained+allocated>MC_INVENTORY_BYTES_MAX)){
            for(unsigned i=0;i<count;i++)free(slots[i].wire);
            free(carried.wire);return bad(inv);
        }
        if(accept){
            if(window==0){for(int i=0;i<46;i++){release(inv,&inv->player[i]);inv->player[i]=slots[i];inv->bytes+=slots[i].size;}inv->player_state_id=state;}
            else{
                for(int i=0;i<MC_INVENTORY_SLOTS;i++)release(inv,&inv->container[i]);
                inv->container_count=count>=36?count-36:count;inv->slot_count=count;
                for(unsigned i=0;i<count;i++){
                    McItem *dst=slot_at(inv,i);release(inv,dst);*dst=slots[i];inv->bytes+=slots[i].size;
                }
            }
            release(inv,&inv->carried);inv->carried=carried;inv->bytes+=carried.size;
            if(window==inv->window_id)inv->state_id=state;
            inv->revision++;inv->pending_until=0;
        }else{
            for(unsigned i=0;i<count;i++)free(slots[i].wire);
            free(carried.wire);
        }
    }
    hotbar(inv,c);return 0;
}
void mc_inventory_view(const McInventory *inv,McInventoryView *view,uint64_t now){
    memset(view,0,sizeof(*view));view->window_id=inv->window_id;view->window_type=inv->window_type;view->slot_count=inv->slot_count;view->container_count=inv->container_count;
    view->revision=inv->revision;view->clicks=inv->clicks;view->bytes=inv->bytes;
    view->state_id=inv->window_id?inv->state_id:inv->player_state_id;view->carried=inv->carried.stack;view->pending=now<inv->pending_until;
    memcpy(view->properties,inv->properties,sizeof(view->properties));memcpy(view->title,inv->title,sizeof(view->title));
    for(int i=0;i<view->slot_count;i++){const McItem *item=mc_inventory_item(inv,i);view->slots[i]=item?item->stack:(McSlot){-1,0};}
}
int mc_inventory_click_packet(McInventory *inv,int window,int slot,int button,int mode,uint64_t now,uint8_t *out,size_t capacity){
    if(window!=inv->window_id || (slot!=-999 && !mc_inventory_item(inv,slot)) || mode<0 || mode>6 || mode==5)return -1;
    if(button<0 || (mode==2?button>8:mode==3?button!=2:mode==6?button!=0:button>1))return -1;
    if(now<inv->pending_until)return 0;
    size_t carry=inv->carried.size?inv->carried.size:1;if(capacity<carry+16)return -1;
    out[0]=0x0b;out[1]=window;size_t n=2;
    n+=mc_varint_write(out+n,inv->window_id?inv->state_id:inv->player_state_id);
    out[n++]=(uint16_t)slot>>8;out[n++]=slot;out[n++]=button;out[n++]=mode;out[n++]=0;
    if(inv->carried.size){memcpy(out+n,inv->carried.wire,inv->carried.size);n+=inv->carried.size;}else out[n++]=0;
    inv->pending_until=now+1000;inv->clicks++;return (int)n;
}
int mc_inventory_close_packet(McInventory *inv,uint8_t out[2]){out[0]=0x0c;out[1]=inv->window_id;close_window(inv);return 2;}

int mc_inventory_plain_hotbar(McInventory *inv,int slot,int item,int count){
    if(slot<0 || slot>8 || item<0 || (unsigned)item>=mc_items_count() || count<0 || count>mc_item_def(item)->stack_size)return -1;
    McItem value=empty_item();
    if(count){
        uint8_t wire[16]={1};size_t n=1+mc_varint_write(wire+1,item);wire[n++]=count;wire[n++]=0;
        if(inv->bytes-inv->player[36+slot].size+n>MC_INVENTORY_BYTES_MAX)return -1;
        value.wire=malloc(n);if(!value.wire)return -1;
        memcpy(value.wire,wire,n);value.size=n;value.stack=(McSlot){item,count};
    }
    release(inv,&inv->player[36+slot]);inv->player[36+slot]=value;inv->bytes+=value.size;inv->revision++;return 0;
}
