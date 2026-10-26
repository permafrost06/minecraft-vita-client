#ifndef MC_INVENTORY_UI_H
#define MC_INVENTORY_UI_H
#include "inventory.h"
typedef struct {int slot;float x,y,size;} McInventoryRect;
unsigned mc_inventory_layout(const McInventoryView *view,McInventoryRect out[MC_INVENTORY_SLOTS]);
int mc_inventory_navigate(const McInventoryRect *rects,unsigned count,int slot,int dx,int dy);
int mc_inventory_option_count(const McInventoryView *view);
int mc_inventory_option_button(const McInventoryView *view,int option);
void mc_inventory_option_label(const McInventoryView *view,int option,char *out,size_t capacity);
#endif
