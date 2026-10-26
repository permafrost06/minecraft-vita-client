#ifndef MC_ITEMS_H
#define MC_ITEMS_H
#include <stdint.h>
#define MC_ITEM_ATLAS_TILES 64
#define MC_ITEM_ATLAS_SIZE (MC_ITEM_ATLAS_TILES*16)
typedef struct { int id;const char *name,*display;int stack_size,state,icon;uint32_t color; } McItemDef;
const McItemDef *mc_item_def(int id);
unsigned mc_items_count(void);
/* Original item icons. Block items reuse original procedural block textures.
   The caller initializes material lookup (mc_mesh_init) first. */
int mc_item_textures_build(uint32_t *pixels);
#endif
