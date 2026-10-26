#include "hud.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
typedef struct {uint8_t p[2048];size_t n;} Buf;
static void u8(Buf *b,unsigned v){b->p[b->n++]=(uint8_t)v;}
static void vi(Buf *b,uint32_t v){b->n+=mc_varint_write(b->p+b->n,v);}
static void str(Buf *b,const char *s){size_t n=strlen(s);vi(b,n);memcpy(b->p+b->n,s,n);b->n+=n;}
static void i32(Buf *b,int32_t v){for(int i=3;i>=0;i--)u8(b,((uint32_t)v>>(i*8))&255);}
static void f32(Buf *b,float v){uint32_t bits;memcpy(&bits,&v,4);i32(b,(int32_t)bits);}
static int send(McHud *h,uint32_t id,Buf *b,uint64_t now){return mc_hud_packet(h,id,b->p,b->n,now);}
static void team(McHud *h,const char *name,unsigned mode,const char *prefix,const char *member){
    Buf b={{0},0};str(&b,name);u8(&b,mode);
    if(mode==0 || mode==2){str(&b,"{\"text\":\"Team\"}");u8(&b,0);str(&b,"always");str(&b,"always");vi(&b,12);str(&b,prefix);str(&b,"\"\"");}
    if(mode==0 || mode==3 || mode==4){vi(&b,member?1:0);if(member)str(&b,member);}
    CHECK(send(h,0x5a,&b,0)==0);
}
static void score(McHud *h,const char *entity,int value){const char *objective="side";
    Buf b={{0},0};str(&b,entity);vi(&b,0);str(&b,objective);vi(&b,(uint32_t)value);CHECK(send(h,0x5b,&b,0)==0);
}
int main(void){
    McHud h;mc_hud_init(&h);
    /* Titles fade in, stay and fade out on the server's tick timing. */
    Buf b={{0},0};i32(&b,10);i32(&b,20);i32(&b,10);CHECK(send(&h,0x60,&b,0)==0);
    b.n=0;str(&b,"{\"text\":\"Round \",\"extra\":[{\"text\":\"2\",\"color\":\"gold\"}]}");CHECK(send(&h,0x5d,&b,0)==0);
    b.n=0;str(&b,"\"\\u00a7aWelcome\"");CHECK(send(&h,0x5f,&b,1000)==0);
    CHECK(!strcmp(h.title,"Welcome") && !strcmp(h.subtitle,"Round 2"));
    CHECK(mc_hud_title_alpha(&h,1250)>0.4f && mc_hud_title_alpha(&h,1250)<0.6f);
    CHECK(mc_hud_title_alpha(&h,2000)==1 && mc_hud_title_alpha(&h,2750)<0.6f && mc_hud_title_alpha(&h,3100)==0);
    b.n=0;u8(&b,0);CHECK(send(&h,0x0e,&b,3000)==0 && !h.title[0]);
    b.n=0;u8(&b,2);CHECK(send(&h,0x0e,&b,0)<0);

    /* Boss bars: add, update, ignore unknown, remove. */
    uint8_t uuid[16]={1,2,3};
    b.n=0;memcpy(b.p,uuid,16);b.n=16;vi(&b,0);str(&b,"{\"text\":\"Ender Dragon\"}");f32(&b,.75f);vi(&b,5);vi(&b,0);u8(&b,1);
    CHECK(send(&h,0x0b,&b,0)==0 && h.bosses[0].active && !strcmp(h.bosses[0].title,"Ender Dragon") && h.bosses[0].color==5);
    b.n=0;memcpy(b.p,uuid,16);b.n=16;vi(&b,2);f32(&b,.25f);CHECK(send(&h,0x0b,&b,0)==0 && h.bosses[0].health==.25f);
    b.n=0;memset(b.p,9,16);b.n=16;vi(&b,2);f32(&b,.5f);CHECK(send(&h,0x0b,&b,0)==0 && !h.bosses[1].active);
    b.n=0;memcpy(b.p,uuid,16);b.n=16;vi(&b,4);vi(&b,7);vi(&b,0);CHECK(send(&h,0x0b,&b,0)<0);
    b.n=0;memcpy(b.p,uuid,16);b.n=16;vi(&b,1);CHECK(send(&h,0x0b,&b,0)==0 && !h.bosses[0].active);

    /* Plugin-style sidebar: invisible holders whose text lives in team prefixes. */
    b.n=0;str(&b,"side");u8(&b,0);str(&b,"{\"text\":\"\\u00a76\\u00a7lMY SERVER\"}");vi(&b,0);CHECK(send(&h,0x58,&b,0)==0);
    b.n=0;u8(&b,1);str(&b,"side");CHECK(send(&h,0x51,&b,0)==0);
    team(&h,"line1",0,"{\"text\":\"Coins: 50\"}","\xc2\xa7""a");
    team(&h,"line2",0,"\"Rank: \\u00a7bVIP\"","\xc2\xa7""b");
    score(&h,"\xc2\xa7""a",2);score(&h,"\xc2\xa7""b",1);score(&h,"#hidden",9);score(&h,"Steve",1);
    McSidebar side;CHECK(mc_hud_sidebar(&h,-1,&side));
    CHECK(!strcmp(side.title,"MY SERVER") && side.count==3);
    CHECK(!strcmp(side.lines[0].text,"Coins: 50") && side.lines[0].value==2);
    CHECK(!strcmp(side.lines[1].text,"Steve") && !strcmp(side.lines[2].text,"Rank: VIP"));
    score(&h,"\xc2\xa7""a",5);CHECK(mc_hud_sidebar(&h,-1,&side) && side.lines[0].value==5);
    team(&h,"line1",4,NULL,"\xc2\xa7""a");CHECK(mc_hud_sidebar(&h,-1,&side) && !strcmp(side.lines[0].text,""));
    /* Removing a holder from all objectives, then the objective itself. */
    b.n=0;str(&b,"Steve");vi(&b,1);str(&b,"");CHECK(send(&h,0x5b,&b,0)==0);
    CHECK(mc_hud_sidebar(&h,-1,&side) && side.count==2);
    b.n=0;str(&b,"side");u8(&b,1);CHECK(send(&h,0x58,&b,0)==0);CHECK(!mc_hud_sidebar(&h,-1,&side) && !h.display[1][0]);
    /* Team removal and malformed team packets. */
    team(&h,"line2",1,NULL,NULL);
    for(int i=0;i<MC_HUD_MEMBERS;i++)CHECK(!h.members[i].active);
    b.n=0;str(&b,"t");u8(&b,9);CHECK(send(&h,0x5a,&b,0)<0);

    /* Tab list header/footer, server data and difficulty. */
    b.n=0;str(&b,"{\"text\":\"Hub\"}");str(&b,"\"play.example\"");CHECK(send(&h,0x65,&b,0)==0 && !strcmp(h.header,"Hub") && !strcmp(h.footer,"play.example"));
    b.n=0;str(&b,"{\"text\":\"A Minecraft Server\"}");u8(&b,1);vi(&b,3);u8(&b,0x89);u8(&b,'P');u8(&b,'N');u8(&b,1);CHECK(send(&h,0x45,&b,0)==0 && !strcmp(h.motd,"A Minecraft Server") && h.secure_chat);
    b.n=0;u8(&b,2);u8(&b,0);CHECK(send(&h,0x0c,&b,0)==0 && h.difficulty==2);
    b.n=0;u8(&b,4);u8(&b,0);CHECK(send(&h,0x0c,&b,0)<0);
    /* Join Game keeps server data but clears everything else. */
    CHECK(mc_hud_packet(&h,0x28,NULL,0,0)==0 && !h.header[0] && !strcmp(h.motd,"A Minecraft Server"));
    char legacy[32];mc_text_legacy("\xc2\xa7""cRed \xc2\xa7lbold\xc3\xa9",legacy,sizeof(legacy));CHECK(!strcmp(legacy,"Red bold?"));
    puts("HUD checks passed: titles, boss bars, sidebar teams/scores, tab list, server data, difficulty");
    return 0;
}
