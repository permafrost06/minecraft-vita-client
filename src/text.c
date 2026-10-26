#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* Bounded plain-text view of 1.19.4 JSON sign components. Formatting and
   translated components are omitted; full wire NBT remains in the cache. */
typedef struct {const char *p;char *out;size_t used,cap;int error;} Json;
static void space(Json *j){while(isspace((unsigned char)*j->p))j->p++;}
static void append(Json *j,char c,int emit){if(emit && j->used+1<j->cap)j->out[j->used++]=c;}
static void string(Json *j,int emit,char *key,size_t cap){
    size_t n=0;if(*j->p++!='"'){j->error=1;return;}
    while(*j->p && *j->p!='"'){
        unsigned char c=*j->p++;
        if(c=='\\'){
            c=*j->p;if(!c){j->error=1;return;}j->p++;
            if(c=='u'){
                for(int i=0;i<4;i++){if(!isxdigit((unsigned char)*j->p)){j->error=1;return;}j->p++;}c='?';
            }else if(c=='n' || c=='r' || c=='t')c=' ';
            else if(c!='"' && c!='\\' && c!='/'){j->error=1;return;}
        }
        if(c<32){j->error=1;return;}
        if(c>=128){if((c&0xc0)==0x80)continue;c='?';}
        append(j,c,emit);if(key && n+1<cap)key[n++]=c;
    }
    if(*j->p!='"'){j->error=1;return;}j->p++;if(key)key[n]=0;
}
static void value(Json *j,int emit,unsigned depth){
    space(j);if(depth>32){j->error=1;return;}
    if(*j->p=='"'){string(j,emit,NULL,0);return;}
    char close=*j->p=='{'?'}':*j->p=='['?']':0;
    if(close){
        int object=*j->p++=='{';space(j);
        if(*j->p==close){j->p++;return;}
        while(!j->error){
            int child=emit;
            if(object){char key[32]={0};space(j);if(*j->p!='"'){j->error=1;break;}string(j,0,key,sizeof(key));space(j);if(*j->p++!=':'){j->error=1;break;}child=emit && (!strcmp(key,"text") || !strcmp(key,"extra") || !strcmp(key,"translate") || !strcmp(key,"with"));}
            value(j,child,depth+1);space(j);if(j->error)break;
            if(*j->p==close){j->p++;return;}
            if(*j->p++!=','){j->error=1;break;}
        }
        return;
    }
    const char *start=j->p;
    while(*j->p && *j->p!=',' && *j->p!='}' && *j->p!=']' && !isspace((unsigned char)*j->p))j->p++;
    if(j->p==start)j->error=1;
}
int mc_text_plain(const uint8_t *p,size_t n,char *out,size_t capacity){
    if(!capacity || n>32767)return -1;
    out[0]=0;char *json=malloc(n+1);if(!json)return -1;
    memcpy(json,p,n);json[n]=0;
    Json j={json,out,0,capacity,0};value(&j,1,0);space(&j);
    int bad=j.error || *j.p || memchr(p,0,n)!=NULL;
    free(json);if(bad){out[0]=0;return -1;}out[j.used]=0;return 0;
}
