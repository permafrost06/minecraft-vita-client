#ifndef MC_RECIPES_H
#define MC_RECIPES_H
#include "reader.h"
#define MC_STONE_RECIPES 512
#define MC_RECIPE_INPUTS 64
typedef struct {int inputs[MC_RECIPE_INPUTS];unsigned count;McSlot output;} McStoneRecipe;
typedef struct {McStoneRecipe stone[MC_STONE_RECIPES];unsigned count;} McRecipeBook;
/* Transactionally replace declared recipes; only stonecutting data is retained. */
int mc_recipes_read(McRecipeBook **book,const uint8_t *p,size_t n);
unsigned mc_stone_choices(const McRecipeBook *book,int input,McSlot out[128]);
#endif
