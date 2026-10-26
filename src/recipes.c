#include "recipes.h"
#include "items.h"
#include <stdlib.h>
#include <string.h>
static void string_skip(McReader *r){unsigned n=mc_varint_read(r);if(n>32767)r->error=1;else mc_skip(r,n);}
static void ingredient(McReader *r,McStoneRecipe *stone){
    unsigned count=mc_varint_read(r);if(count>MC_RECIPE_INPUTS){r->error=1;return;}
    for(unsigned i=0;i<count && !r->error;i++){McSlot slot=mc_read_slot(r);if(stone && slot.count)stone->inputs[stone->count++]=slot.item;}
}
int mc_recipes_read(McRecipeBook **book,const uint8_t *p,size_t n){
    McRecipeBook *next=calloc(1,sizeof(*next));if(!next)return -1;
    McReader r={p,n,0,0};unsigned count=mc_varint_read(&r);if(count>8192)r.error=1;
    for(unsigned i=0;i<count && !r.error;i++){
        char type[128];mc_string(&r,type,sizeof(type));string_skip(&r);if(r.error)break;
        const char *t=!strncmp(type,"minecraft:",10)?type+10:type;
        if(!strcmp(t,"stonecutting")){
            if(next->count==MC_STONE_RECIPES){r.error=1;break;}
            McStoneRecipe *s=&next->stone[next->count++];string_skip(&r);ingredient(&r,s);s->output=mc_read_slot(&r);
        }else if(!strcmp(t,"crafting_shaped") || !strcmp(t,"crafting_shapeless")){
            unsigned slots=0;
            if(!strcmp(t,"crafting_shaped")){unsigned w=mc_varint_read(&r),h=mc_varint_read(&r);if(w>3 || h>3){r.error=1;break;}slots=w*h;}
            string_skip(&r);mc_varint_read(&r);
            if(!strcmp(t,"crafting_shapeless"))slots=mc_varint_read(&r);
            if(slots>9){r.error=1;break;}
            for(unsigned j=0;j<slots;j++)ingredient(&r,NULL);
            mc_read_slot(&r);if(!strcmp(t,"crafting_shaped") && mc_u8(&r)>1)r.error=1;
        }else if(!strncmp(t,"crafting_special_",17) || !strcmp(t,"crafting_decorated_pot")){mc_varint_read(&r);}
        else if(!strcmp(t,"smelting") || !strcmp(t,"blasting") || !strcmp(t,"smoking") || !strcmp(t,"campfire_cooking")){
            string_skip(&r);mc_varint_read(&r);ingredient(&r,NULL);mc_read_slot(&r);mc_f32(&r);mc_varint_read(&r);
        }else if(!strcmp(t,"smithing") || !strcmp(t,"smithing_transform") || !strcmp(t,"smithing_trim")){
            ingredient(&r,NULL);ingredient(&r,NULL);if(strcmp(t,"smithing"))ingredient(&r,NULL);
            if(strcmp(t,"smithing_trim"))mc_read_slot(&r);
        }else r.error=1;
    }
    if(r.error || r.pos!=n){free(next);return -1;}
    /* Vanilla sorts matching recipes by result description ID. Stonecutting
       outputs are block items, so their identifier names give the same order.
       Stable insertion retains wire ordering for equal outputs. */
    for(unsigned i=1;i<next->count;i++){
        McStoneRecipe value=next->stone[i];unsigned j=i;
        while(j && strcmp(mc_item_def(next->stone[j-1].output.item)->name,mc_item_def(value.output.item)->name)>0){next->stone[j]=next->stone[j-1];j--;}
        next->stone[j]=value;
    }
    free(*book);*book=next;return 0;
}
unsigned mc_stone_choices(const McRecipeBook *book,int input,McSlot out[128]){
    if(!book || input<0)return 0;
    unsigned count=0;
    for(unsigned i=0;i<book->count;i++)for(unsigned j=0;j<book->stone[i].count;j++)if(book->stone[i].inputs[j]==input){
        if(count<128)out[count++]=book->stone[i].output;
        break;
    }
    return count;
}
