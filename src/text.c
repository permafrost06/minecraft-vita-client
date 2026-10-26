#include "text.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
/* Plain ASCII view of bounded JSON components. Formatting is ignored. Unknown
   translations retain their key and arguments. No signature verification. */
#define TEXT 384
#define ARGS 16
typedef struct {const char *p;int error,newlines;} Json;
typedef struct {char *p;size_t used,cap;} Output;
static void space(Json *j){while(isspace((unsigned char)*j->p))j->p++;}
static void put(Output *o,char ch){if(o->used+1<o->cap)o->p[o->used++]=ch;}
static void add(Output *o,const char *s){while(*s)put(o,*s++);}
static void string(Json *j,Output *o){
    if(*j->p!='"'){j->error=1;return;}j->p++;
    int legacy=0; /* Section-sign formatting codes are dropped with their code character. */
    while(*j->p && *j->p!='"'){
        unsigned char ch=*j->p++;
        if(ch=='\\'){
            ch=*j->p;if(!ch){j->error=1;return;}j->p++;
            if(ch=='u'){unsigned code=0;for(int i=0;i<4;i++){unsigned c=(unsigned char)*j->p;if(!isxdigit(c)){j->error=1;return;}code=code*16+(c<='9'?c-'0':tolower(c)-'a'+10);j->p++;}
                if(code==0xa7){legacy=1;continue;}ch=code>=32 && code<127?code:'?';}
            else if(ch=='n' && j->newlines)ch='\n';
            else if(ch=='n' || ch=='r' || ch=='t' || ch=='b' || ch=='f')ch=' ';
            else if(ch!='"' && ch!='\\' && ch!='/'){j->error=1;return;}
        }else if(ch<32){j->error=1;return;}
        else if(ch==0xc2 && (unsigned char)*j->p==0xa7){j->p++;legacy=1;continue;}
        if(legacy){legacy=0;if(ch<128)continue;}
        if(ch>=128){if((ch&0xc0)==0x80)continue;ch='?';}
        put(o,ch);
    }
    if(*j->p!='"'){j->error=1;return;}j->p++;
}
static const char *translation(const char *key){
    static const char *const table[][2]={
        {"chat.type.text","<%s> %s"},{"chat.type.announcement","[%s] %s"},{"chat.type.emote","* %s %s"},
        {"chat.type.team.text","%s <%s> %s"},{"chat.type.team.sent","-> %s <%s> %s"},{"commands.message.display.incoming","%s whispers to you: %s"},
        {"commands.message.display.outgoing","You whisper to %s: %s"},{"multiplayer.player.joined","%s joined the game"},
        {"multiplayer.player.left","%s left the game"},{"gamemode.creative","Creative Mode"},{"gamemode.survival","Survival Mode"},{"gamemode.adventure","Adventure Mode"},{"gamemode.spectator","Spectator Mode"},{"gameMode.creative","Creative Mode"},{"gameMode.survival","Survival Mode"},
        {"gameMode.adventure","Adventure Mode"},{"gameMode.spectator","Spectator Mode"},{"gamemode.changed","Your game mode is now %s"},{"gameMode.changed","Your game mode has been updated to %s"},
        {"commands.gamemode.success.self","Set own game mode to %s"},{"commands.gamemode.success.other","Set %s's game mode to %s"},
        {"commands.time.set","Set the time to %s"},{"commands.give.success.single","Gave %s to %s"},
        {"commands.enchant.success.single","Applied %s to %s"},{"commands.seed.success","Seed: %s"}
    };
    for(unsigned i=0;i<sizeof(table)/sizeof(table[0]);i++)if(!strcmp(key,table[i][0]))return table[i][1];
    return NULL;
}
static void value(Json *j,Output *o,unsigned depth);
static void read_into(Json *j,char *out,size_t cap,unsigned depth){Output o={out,0,cap};value(j,&o,depth);out[o.used]=0;}
static void object(Json *j,Output *out,unsigned depth){
    typedef struct {char text[TEXT],extra[TEXT],translate[128],fallback[TEXT],args[ARGS][TEXT];unsigned count;} Object;
    Object *v=calloc(1,sizeof(*v));if(!v){j->error=1;return;}
    j->p++;space(j);
    while(!j->error && *j->p!='}'){
        char key[64]={0};Output k={key,0,sizeof(key)};string(j,&k);space(j);if(j->error || *j->p!=':'){j->error=1;break;}j->p++;space(j);
        if(!strcmp(key,"with") && *j->p=='['){
            j->p++;space(j);
            while(!j->error && *j->p!=']'){
                if(v->count==ARGS){j->error=1;break;}
                read_into(j,v->args[v->count++],TEXT,depth+1);space(j);
                if(*j->p==']')break;
                if(*j->p!=','){j->error=1;break;}j->p++;space(j);if(*j->p==']'){j->error=1;break;}
            }
            if(*j->p==']')j->p++;else j->error=1;
        }else if(!strcmp(key,"text"))read_into(j,v->text,TEXT,depth+1);
        else if(!strcmp(key,"extra"))read_into(j,v->extra,TEXT,depth+1);
        else if(!strcmp(key,"translate"))read_into(j,v->translate,sizeof(v->translate),depth+1);
        else if(!strcmp(key,"fallback"))read_into(j,v->fallback,TEXT,depth+1);
        else{char discard[1];read_into(j,discard,sizeof(discard),depth+1);}
        space(j);if(*j->p=='}')break;
        if(j->error || *j->p!=','){j->error=1;break;}j->p++;space(j);if(*j->p=='}'){j->error=1;break;}
    }
    if(*j->p=='}')j->p++;else j->error=1;
    if(v->translate[0]){
        const char *format=translation(v->translate);if(!format && v->fallback[0])format=v->fallback;
        if(format){unsigned arg=0;while(*format){if(format[0]=='%' && format[1]=='s'){if(arg<v->count)add(out,v->args[arg++]);format+=2;}else if(format[0]=='%' && format[1]=='%'){put(out,'%');format+=2;}else put(out,*format++);}}
        else{add(out,v->translate);for(unsigned i=0;i<v->count;i++){put(out,' ');add(out,v->args[i]);}}
    }else add(out,v->text);
    add(out,v->extra);free(v);
}
static void value(Json *j,Output *o,unsigned depth){
    space(j);if(depth>32 || j->error){j->error=1;return;}
    if(*j->p=='"'){string(j,o);return;}
    if(*j->p=='{'){object(j,o,depth);return;}
    if(*j->p=='['){
        j->p++;space(j);if(*j->p==']'){j->p++;return;}
        while(!j->error){value(j,o,depth+1);space(j);if(*j->p==']'){j->p++;return;}
            if(j->error || *j->p!=','){j->error=1;return;}j->p++;space(j);if(*j->p==']'){j->error=1;return;}}
        return;
    }
    const char *start=j->p;while(*j->p && *j->p!=',' && *j->p!='}' && *j->p!=']' && !isspace((unsigned char)*j->p))j->p++;
    size_t n=j->p-start;
    if((n==4 && !memcmp(start,"true",4)) || (n==5 && !memcmp(start,"false",5)) || (n==4 && !memcmp(start,"null",4)))return;
    char *end;strtod(start,&end);if(!n || end!=j->p)j->error=1;
}
static int plain(const uint8_t *p,size_t n,char *out,size_t capacity,int newlines){
    if(!capacity || n>32767)return -1;
    out[0]=0;if(memchr(p,0,n))return -1;char *json=malloc(n+1);if(!json)return -1;
    memcpy(json,p,n);json[n]=0;Json j={json,0,newlines};Output o={out,0,capacity};value(&j,&o,0);space(&j);
    int bad=j.error || *j.p;free(json);if(bad){out[0]=0;return -1;}out[o.used]=0;return 0;
}
int mc_text_plain(const uint8_t *p,size_t n,char *out,size_t capacity){return plain(p,n,out,capacity,0);}
int mc_text_multiline(const uint8_t *p,size_t n,char *out,size_t capacity){return plain(p,n,out,capacity,1);}
static void legacy(const char *in,char *out,size_t capacity,int newlines){
    if(!capacity)return;
    size_t used=0;const unsigned char *p=(const unsigned char *)in;
    while(*p && used+1<capacity){
        if(p[0]==0xc2 && p[1]==0xa7){p+=2;if(*p && *p<128)p++;continue;}
        unsigned char ch=*p++;
        if(ch>=128){if((ch&0xc0)==0x80)continue;ch='?';}else if(ch=='\n' && newlines){}else if(ch<32)ch=' ';
        out[used++]=(char)ch;
    }
    out[used]=0;
}
void mc_text_legacy(const char *in,char *out,size_t capacity){legacy(in,out,capacity,0);}
void mc_text_legacy_lines(const char *in,char *out,size_t capacity){legacy(in,out,capacity,1);}
const char *mc_text_translation(const char *key){return translation(key);}
