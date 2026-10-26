#ifndef MC_ENTITY_MESH_H
#define MC_ENTITY_MESH_H
#include "entities.h"
/* World-relative model vertices in block units; colors in RGBA memory order. */
typedef struct { float x,y,z;uint32_t color; } McEntityVertex;
#define MC_ENTITY_MODEL_VERTICES (36*10)
/* Original cuboid models, with dimensions from the entity registry. Returns 0
   for invisible entities or insufficient output space. No global scratch. */
unsigned mc_entity_model(const McEntity *e,uint64_t now,McEntityVertex *out,unsigned capacity);
#endif
