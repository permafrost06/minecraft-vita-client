#include "chat.h"
#include "text.h"
#include <string.h>
#include <stdio.h>
static int boolean(McReader *r){int b=mc_u8(r);if(b>1)r->error=1;return b;}
static void component(McReader *r,char *out,size_t cap){
    unsigned n=mc_varint_read(r);
    if(r->error || n>32767 || n>r->n-r->pos){r->error=1;return;}
    if(mc_text_plain(r->p+r->pos,n,out,cap)<0)r->error=1;
    mc_skip(r,n);
}
static void plain(McReader *r,char *out,size_t cap){
    unsigned n=mc_varint_read(r);if(r->error || n>1024 || n>r->n-r->pos){r->error=1;return;}
    size_t at=0;for(unsigned i=0;i<n;i++){unsigned ch=mc_u8(r);if((ch&0xc0)==0x80)continue;if(at+1<cap)out[at++]=ch>=128?'?':ch<32?' ':ch;}out[at]=0;
}
void mc_chat_init(McChat *c){memset(c,0,sizeof(*c));}
int mc_chat_packet(McChat *c,uint32_t id,const uint8_t *p,size_t n,uint64_t now){
    if(id==0x28){mc_chat_init(c);return 0;}
    if(id!=0x64 && id!=0x46 && id!=0x1b && id!=0x35)return 0;
    McReader r={p,n,0,0};char text[MC_CHAT_TEXT]={0},name[128]={0},target[128]={0};int action=id==0x46,signed_message=0;
    if(id==0x64 || id==0x46){component(&r,text,sizeof(text));if(id==0x64)action=boolean(&r);}
    else{
        if(id==0x35){
            mc_skip(&r,16);mc_varint_read(&r);signed_message=boolean(&r);if(signed_message)mc_skip(&r,256);
            plain(&r,text,sizeof(text));
            mc_u64(&r);mc_u64(&r);unsigned count=mc_varint_read(&r);if(count>20)return -1;
            for(unsigned i=0;i<count;i++)if(!mc_varint_read(&r))mc_skip(&r,256);
            if(boolean(&r))component(&r,text,sizeof(text));
            unsigned filter=mc_varint_read(&r);if(filter>2)return -1;
            if(filter==2){unsigned words=mc_varint_read(&r);if(words>64)return -1;mc_skip(&r,words*8);}
            /* Do not show text the server marked fully or partially filtered. */
            if(filter)snprintf(text,sizeof(text),"[filtered message]");
        }else component(&r,text,sizeof(text));
        mc_varint_read(&r);component(&r,name,sizeof(name));if(boolean(&r))component(&r,target,sizeof(target));
    }
    if(r.error || r.pos!=n)return -1;
    if(signed_message)c->signed_pending++;
    if(action){snprintf(c->action,sizeof(c->action),"%s",text);c->action_at=now;}
    else{
        McChatLine *line=&c->lines[c->next];
        if(name[0])snprintf(line->text,sizeof(line->text),"<%.100s> %.278s",name,text);
        else snprintf(line->text,sizeof(line->text),"%s",text);
        line->at=now;c->next=(c->next+1)%MC_CHAT_LINES;if(c->count<MC_CHAT_LINES)c->count++;
    }
    return 0;
}
int mc_chat_encode(const char *text,uint64_t timestamp,unsigned offset,uint8_t *out,size_t cap){
    size_t len=strlen(text);if(!len || len>256 || cap<len+32)return -1;
    for(size_t i=0;i<len;i++)if((unsigned char)text[i]<32 || (unsigned char)text[i]>126)return -1;
    int command=text[0]=='/';if(command){text++;len--;if(!len)return -1;}
    size_t n=0;out[n++]=command?4:5;n+=mc_varint_write(out+n,len);memcpy(out+n,text,len);n+=len;
    for(int i=7;i>=0;i--)out[n++]=timestamp>>(8*i);
    memset(out+n,0,8);n+=8;out[n++]=0; /* no argument signatures / no signature */
    n+=mc_varint_write(out+n,offset);memset(out+n,0,3);return n+3;
}
