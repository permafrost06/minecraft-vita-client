#ifndef MC_INVENTORY_H
#define MC_INVENTORY_H
#include "reader.h"
#include "recipes.h"
enum {MC_MENU_ANVIL=7,MC_MENU_BEACON=8,MC_MENU_BLAST_FURNACE=9,MC_MENU_BREWING=10,MC_MENU_CRAFTING=11,MC_MENU_ENCHANTMENT=12,MC_MENU_FURNACE=13,MC_MENU_GRINDSTONE=14,MC_MENU_HOPPER=15,MC_MENU_LECTERN=16,MC_MENU_LOOM=17,MC_MENU_MERCHANT=18,MC_MENU_LEGACY_SMITHING=20,MC_MENU_SMITHING=21,MC_MENU_SMOKER=22,MC_MENU_CARTOGRAPHY=23,MC_MENU_STONECUTTER=24};
#define MC_TRADE_MAX 64
typedef struct {McSlot buy,sell,second;int disabled,uses,max_uses,xp,special,demand;float multiplier;} McTrade;
#define MC_INVENTORY_SLOTS 128
#define MC_ITEM_WIRE_MAX 32768
#define MC_INVENTORY_BYTES_MAX (512*1024)
typedef struct { McSlot stack;uint8_t *wire;size_t size; } McItem;
typedef struct {
    McRecipeBook *recipes;
    McItem player[46],container[MC_INVENTORY_SLOTS],carried;
    int window_id,window_type,container_count,slot_count,state_id,player_state_id;
    int properties[16];
    McTrade trades[MC_TRADE_MAX];int trade_count,trade_level,trade_xp;
    char title[128],error[128];
    size_t bytes;
    uint64_t revision,clicks,pending_until;
} McInventory;
typedef struct {
    McSlot slots[MC_INVENTORY_SLOTS],carried,stone_choices[128];int stone_count;
    int window_id,window_type,slot_count,container_count,state_id,properties[16],pending;
    McTrade trades[MC_TRADE_MAX];int trade_count,trade_level,trade_xp;
    char title[128];
    uint64_t revision,clicks;size_t bytes;
} McInventoryView;
void mc_inventory_init(McInventory *inv);
void mc_inventory_destroy(McInventory *inv);
int mc_inventory_packet(McInventory *inv,McClient *c,uint32_t id,const uint8_t *p,size_t n);
const McItem *mc_inventory_item(const McInventory *inv,int slot);
void mc_inventory_view(const McInventory *inv,McInventoryView *view,uint64_t now);
/* Server executes the click; the client waits for its authoritative slot/cursor
   updates. Empty changed-slot prediction intentionally requests reconciliation. */
int mc_inventory_click_packet(McInventory *inv,int window,int slot,int button,int mode,uint64_t now,uint8_t *out,size_t capacity);
/* Root compound of an item's NBT; -1 when the stack has no tag. */
#include "nbt.h"
int mc_item_tag(const McItem *item,McNbt *tag);
int mc_item_enchantment(const McItem *item,const char *name);
int mc_item_can_destroy(const McItem *item,const char *block_name);
int mc_inventory_plain_hotbar(McInventory *inv,int slot,int item,int count);
int mc_inventory_button_packet(McInventory *inv,int window,int button,uint64_t now,uint8_t out[8]);
int mc_inventory_rename_packet(McInventory *inv,int window,const char *name,uint8_t *out,size_t capacity);
int mc_inventory_beacon_packet(McInventory *inv,int window,int primary,int secondary,uint8_t out[8]);
int mc_inventory_close_packet(McInventory *inv,uint8_t out[2]);
#endif
