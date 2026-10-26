#ifndef MC_INVENTORY_RENDER_H
#define MC_INVENTORY_RENDER_H
#include "inventory_ui.h"
void mc_inventory_draw(const McInventoryView *view,int selected,int catalog,int catalog_item,int hotbar,const McSlot hotbar_slots[9],int creative,int option_focus,int option);
#endif
