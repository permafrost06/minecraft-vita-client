#include "hud.h"
#include "text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
void mc_hud_init(McHud *hud){memset(hud,0,sizeof(*hud));hud->fade_in=10;hud->stay=70;hud->fade_out=20;hud->difficulty=-1;}
static void copy(char *out,size_t capacity,const char *in){size_t n=strnlen(in,capacity-1);memcpy(out,in,n);out[n]=0;}
static int bad(McHud *hud,const char *what){snprintf(hud->error,sizeof(hud->error),"%s",what);return -1;}
static int boolean(McReader *r){unsigned v=mc_u8(r);if(v>1)r->error=1;return (int)v;}
/* JSON text component; unparseable text falls back to the raw wire string. */
static void component(McReader *r,char *out,size_t capacity){
    out[0]=0;uint32_t n=mc_varint_read(r);
    if(r->error || n>r->n-r->pos || n>262144){r->error=1;return;}
    if(mc_text_plain(r->p+r->pos,n,out,capacity)<0){
        char raw[512];size_t copy=n<sizeof(raw)-1?n:sizeof(raw)-1;memcpy(raw,r->p+r->pos,copy);raw[copy]=0;
        mc_text_legacy(raw,out,capacity);
    }
    r->pos+=n;
}
/* Bounded identifier string (team, objective or score holder name). */
static void name(McReader *r,char *out,size_t capacity,uint32_t limit){
    uint32_t n=mc_varint_read(r);
    if(r->error || n>r->n-r->pos || n>limit*4){r->error=1;out[0]=0;return;}
    size_t copy=n<capacity-1?n:capacity-1;memcpy(out,r->p+r->pos,copy);out[copy]=0;r->pos+=n;
    for(size_t i=0;i<copy;i++)if(!out[i]){r->error=1;return;}
}
static McTeam *team(McHud *hud,const char *key){for(int i=0;i<MC_HUD_TEAMS;i++)if(hud->teams[i].active && !strcmp(hud->teams[i].name,key))return &hud->teams[i];return NULL;}
static McObjective *objective(McHud *hud,const char *key){for(int i=0;i<MC_HUD_OBJECTIVES;i++)if(hud->objectives[i].active && !strcmp(hud->objectives[i].name,key))return &hud->objectives[i];return NULL;}
static void join_team(McHud *hud,int index,const char *member){
    McTeamMember *slot=NULL;
    for(int i=0;i<MC_HUD_MEMBERS;i++){McTeamMember *m=&hud->members[i];if(m->active && !strcmp(m->name,member)){slot=m;break;}if(!m->active && !slot)slot=m;}
    if(!slot){hud->overflow++;return;}
    slot->active=1;slot->team=index;copy(slot->name,sizeof(slot->name),member);
}
static void leave_team(McHud *hud,int index,const char *member){
    for(int i=0;i<MC_HUD_MEMBERS;i++){McTeamMember *m=&hud->members[i];if(m->active && m->team==index && !strcmp(m->name,member))m->active=0;}
}
static int teams_packet(McHud *hud,McReader *r){
    char key[64];name(r,key,sizeof(key),16);unsigned mode=mc_u8(r);
    if(r->error || mode>4)return bad(hud,"Invalid team packet");
    McTeam *t=team(hud,key);int index=t?(int)(t-hud->teams):-1;
    if(mode==0 && !t){
        for(int i=0;i<MC_HUD_TEAMS;i++)if(!hud->teams[i].active){t=&hud->teams[i];index=i;break;}
        if(t){memset(t,0,sizeof(*t));t->active=1;copy(t->name,sizeof(t->name),key);}else hud->overflow++;
    }
    if(mode==1){
        if(t){t->active=0;for(int i=0;i<MC_HUD_MEMBERS;i++)if(hud->members[i].team==index)hud->members[i].active=0;}
        return r->pos==r->n?0:bad(hud,"Invalid team removal");
    }
    if(mode==0 || mode==2){
        char display[MC_HUD_TEXT],prefix[64],suffix[64],skip[48];
        component(r,display,sizeof(display));mc_u8(r);name(r,skip,sizeof(skip),40);name(r,skip,sizeof(skip),40);
        unsigned color=mc_varint_read(r);component(r,prefix,sizeof(prefix));component(r,suffix,sizeof(suffix));
        if(r->error || color>21)return bad(hud,"Invalid team info");
        if(t){t->color=(int)color;memcpy(t->prefix,prefix,sizeof(prefix));memcpy(t->suffix,suffix,sizeof(suffix));}
    }
    if(mode==0 || mode==3 || mode==4){
        uint32_t count=mc_varint_read(r);if(r->error || count>65536)return bad(hud,"Invalid team members");
        for(uint32_t i=0;i<count && !r->error;i++){
            char member[48];name(r,member,sizeof(member),40);
            if(!r->error && t){if(mode==4)leave_team(hud,index,member);else join_team(hud,index,member);}
        }
    }
    return r->error || r->pos!=r->n?bad(hud,"Invalid team packet"):0;
}
static int boss_packet(McHud *hud,McReader *r){
    uint8_t uuid[16];if(r->n-r->pos<16)return bad(hud,"Invalid boss bar");
    memcpy(uuid,r->p+r->pos,16);r->pos+=16;unsigned action=mc_varint_read(r);
    if(r->error || action>5)return bad(hud,"Invalid boss bar action");
    McBossBar *bar=NULL,*free_slot=NULL;
    for(int i=0;i<MC_HUD_BOSSES;i++){McBossBar *b=&hud->bosses[i];if(b->active && !memcmp(b->uuid,uuid,16)){bar=b;break;}if(!b->active && !free_slot)free_slot=b;}
    McBossBar value=bar?*bar:(McBossBar){0};
    if(action==0 || action==3)component(r,value.title,sizeof(value.title));
    if(action==0 || action==2){value.health=mc_f32(r);if(!isfinite(value.health))r->error=1;value.health=fmaxf(0,fminf(1,value.health));}
    if(action==0 || action==4){value.color=(int)mc_varint_read(r);value.division=(int)mc_varint_read(r);if(value.color<0 || value.color>6 || value.division<0 || value.division>4)r->error=1;}
    if(action==0 || action==5)value.flags=mc_u8(r);
    if(r->error || r->pos!=r->n)return bad(hud,"Invalid boss bar");
    if(action==1){if(bar)bar->active=0;return 0;}
    if(action==0 && !bar){if(!free_slot){hud->overflow++;return 0;}bar=free_slot;}
    if(!bar)return 0; /* Updates for unknown bars are ignored like vanilla. */
    value.active=1;memcpy(value.uuid,uuid,16);*bar=value;return 0;
}
static int score_packet(McHud *hud,McReader *r){
    char entity[64],key[64];name(r,entity,sizeof(entity),40);unsigned action=mc_varint_read(r);name(r,key,sizeof(key),16);
    int value=0;if(action==0)value=(int)mc_varint_read(r);
    if(r->error || action>1 || r->pos!=r->n)return bad(hud,"Invalid score");
    McScore *slot=NULL;
    for(int i=0;i<MC_HUD_SCORES;i++){
        McScore *s=&hud->scores[i];
        if(!s->active){if(!slot && action==0)slot=s;continue;}
        if(strcmp(s->entity,entity))continue;
        /* Removal with an empty objective name clears every score of the holder. */
        if(action==1 && (!key[0] || !strcmp(s->objective,key)))s->active=0;
        else if(action==0 && !strcmp(s->objective,key)){s->value=value;return 0;}
    }
    if(action==0){
        if(!slot){hud->overflow++;return 0;}
        slot->active=1;slot->value=value;copy(slot->entity,sizeof(slot->entity),entity);copy(slot->objective,sizeof(slot->objective),key);
    }
    return 0;
}
static int objective_packet(McHud *hud,McReader *r){
    char key[64];name(r,key,sizeof(key),16);unsigned mode=mc_u8(r);
    if(r->error || mode>2)return bad(hud,"Invalid objective");
    McObjective *o=objective(hud,key);
    if(mode==1){
        if(o){o->active=0;for(int i=0;i<MC_HUD_SCORES;i++)if(!strcmp(hud->scores[i].objective,key))hud->scores[i].active=0;
            for(int i=0;i<19;i++)if(!strcmp(hud->display[i],key))hud->display[i][0]=0;}
        return r->pos==r->n?0:bad(hud,"Invalid objective removal");
    }
    char display[MC_HUD_TEXT];component(r,display,sizeof(display));unsigned type=mc_varint_read(r);
    if(r->error || type>1 || r->pos!=r->n)return bad(hud,"Invalid objective");
    if(!o && mode==0){for(int i=0;i<MC_HUD_OBJECTIVES;i++)if(!hud->objectives[i].active){o=&hud->objectives[i];break;}
        if(!o){hud->overflow++;return 0;}memset(o,0,sizeof(*o));o->active=1;copy(o->name,sizeof(o->name),key);}
    if(o){memcpy(o->display,display,sizeof(display));o->type=(int)type;}
    return 0;
}
int mc_hud_packet(McHud *hud,uint32_t id,const uint8_t *p,size_t n,uint64_t now){
    McReader r={p,n,0,0};
    if(id==0x28){McHud fresh;mc_hud_init(&fresh);memcpy(fresh.motd,hud->motd,sizeof(fresh.motd));fresh.secure_chat=hud->secure_chat;*hud=fresh;return 0;}
    if(id==0x41){hud->title[0]=hud->subtitle[0]=0;hud->title_at=0;return 0;}
    int result=0;
    switch(id){
    case 0x0b:result=boss_packet(hud,&r);break;
    case 0x0c:{unsigned d=mc_u8(&r);int locked=boolean(&r);if(r.error || d>3 || r.pos!=n)return bad(hud,"Invalid difficulty");hud->difficulty=(int)d;hud->difficulty_locked=locked;break;}
    case 0x0e:{int reset=boolean(&r);if(r.error || r.pos!=n)return bad(hud,"Invalid clear titles");
        hud->title[0]=hud->subtitle[0]=0;hud->title_at=0;if(reset){hud->fade_in=10;hud->stay=70;hud->fade_out=20;}break;}
    case 0x45:{char motd[MC_HUD_TEXT];component(&r,motd,sizeof(motd)); /* 1.19.4: required MOTD, optional icon bytes. */
        if(boolean(&r)){uint32_t icon=mc_varint_read(&r);if(r.error || icon>r.n-r.pos)return bad(hud,"Invalid server data");r.pos+=icon;}
        int secure=boolean(&r);if(r.error || r.pos!=n)return bad(hud,"Invalid server data");
        memcpy(hud->motd,motd,sizeof(motd));hud->secure_chat=secure;break;}
    case 0x51:{unsigned slot=mc_u8(&r);char key[64];name(&r,key,sizeof(key),16);
        if(r.error || slot>18 || r.pos!=n)return bad(hud,"Invalid display objective");
        copy(hud->display[slot],sizeof(hud->display[slot]),key);break;}
    case 0x58:result=objective_packet(hud,&r);break;
    case 0x5a:result=teams_packet(hud,&r);break;
    case 0x5b:result=score_packet(hud,&r);break;
    case 0x5d:{char text[MC_HUD_TEXT];component(&r,text,sizeof(text));if(r.error || r.pos!=n)return bad(hud,"Invalid subtitle");memcpy(hud->subtitle,text,sizeof(text));break;}
    case 0x5f:{char text[MC_HUD_TEXT];component(&r,text,sizeof(text));if(r.error || r.pos!=n)return bad(hud,"Invalid title");memcpy(hud->title,text,sizeof(text));hud->title_at=now;break;}
    case 0x60:{int in=(int32_t)mc_u32(&r),stay=(int32_t)mc_u32(&r),out=(int32_t)mc_u32(&r);
        if(r.error || r.pos!=n)return bad(hud,"Invalid title times");
        hud->fade_in=in<0?0:in>72000?72000:in;hud->stay=stay<0?0:stay>72000?72000:stay;hud->fade_out=out<0?0:out>72000?72000:out;break;}
    case 0x65:{char header[256],footer[256];component(&r,header,sizeof(header));component(&r,footer,sizeof(footer));
        if(r.error || r.pos!=n)return bad(hud,"Invalid tab list");
        memcpy(hud->header,header,sizeof(header));memcpy(hud->footer,footer,sizeof(footer));break;}
    default:return 0;
    }
    if(!result)hud->updates++;
    return result;
}
float mc_hud_title_alpha(const McHud *hud,uint64_t now){
    if(!hud->title[0] || !hud->title_at || now<hud->title_at)return 0;
    double t=(now-hud->title_at)/50.0,in=hud->fade_in,stay=hud->stay,out=hud->fade_out;
    if(t<in)return in>0?(float)(t/in):1;
    if(t<in+stay)return 1;
    if(t<in+stay+out)return out>0?(float)(1-(t-in-stay)/out):0;
    return 0;
}
int mc_hud_team_color(const McHud *hud,const char *holder){
    for(int i=0;i<MC_HUD_MEMBERS;i++){const McTeamMember *m=&hud->members[i];if(m->active && hud->teams[m->team].active && !strcmp(m->name,holder))return hud->teams[m->team].color;}
    return -1;
}
void mc_hud_decorated(const McHud *hud,const char *holder,char *out,size_t capacity){
    char plain[64];mc_text_legacy(holder,plain,sizeof(plain));
    for(int i=0;i<MC_HUD_MEMBERS;i++){const McTeamMember *m=&hud->members[i];
        if(m->active && hud->teams[m->team].active && !strcmp(m->name,holder)){const McTeam *t=&hud->teams[m->team];snprintf(out,capacity,"%s%s%s",t->prefix,plain,t->suffix);return;}}
    snprintf(out,capacity,"%s",plain);
}
int mc_hud_sidebar(const McHud *hud,int team_color,McSidebar *out){
    memset(out,0,sizeof(*out));
    const char *key=team_color>=0 && team_color<16 && hud->display[3+team_color][0]?hud->display[3+team_color]:hud->display[1];
    if(!key[0])return 0;
    const McObjective *o=NULL;for(int i=0;i<MC_HUD_OBJECTIVES;i++)if(hud->objectives[i].active && !strcmp(hud->objectives[i].name,key))o=&hud->objectives[i];
    if(!o)return 0;
    snprintf(out->title,sizeof(out->title),"%s",o->display);
    /* Top to bottom: highest score first, ties by case-insensitive name as
       vanilla draws them. Holders starting with '#' are hidden. */
    const McScore *picked[MC_HUD_SIDEBAR];unsigned count=0;
    for(int i=0;i<MC_HUD_SCORES;i++){
        const McScore *s=&hud->scores[i];
        if(!s->active || strcmp(s->objective,key) || s->entity[0]=='#')continue;
        unsigned at=count;
        while(at>0 && (picked[at-1]->value<s->value || (picked[at-1]->value==s->value && strcasecmp(picked[at-1]->entity,s->entity)>0)))at--;
        if(at>=MC_HUD_SIDEBAR)continue;
        unsigned last=count<MC_HUD_SIDEBAR?count:MC_HUD_SIDEBAR-1;
        for(unsigned j=last;j>at;j--)picked[j]=picked[j-1];
        picked[at]=s;if(count<MC_HUD_SIDEBAR)count++;
    }
    for(unsigned i=0;i<count;i++){mc_hud_decorated(hud,picked[i]->entity,out->lines[i].text,sizeof(out->lines[i].text));out->lines[i].value=picked[i]->value;}
    out->count=count;return 1;
}
