#ifndef MC_NBT_H
#define MC_NBT_H
#include "reader.h"
typedef struct { uint8_t type; const uint8_t *data; size_t size; } McNbt;
int mc_nbt_payload(McReader *r, unsigned type, unsigned depth, McNbt *node);
int mc_nbt_root(McReader *r, McNbt *node);
int mc_nbt_find(McNbt compound, const char *key, McNbt *node);
int mc_nbt_string(McNbt node, char *out, size_t capacity);
int mc_nbt_int(McNbt node, int *value);
#endif
