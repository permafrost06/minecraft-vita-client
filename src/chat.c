#include "chat.h"
#include "text.h"
#include "nbt.h"
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
static uint64_t signature_hash(const uint8_t *p){uint64_t h=1469598103934665603ull;for(int i=0;i<256;i++){h^=p[i];h*=1099511628211ull;}return h?h:1;}
/* Packed signature: 0 + full 256 bytes, or a 1-based index into the signature cache. */
static uint64_t packed_signature(const McChat *c,McReader *r){
    uint32_t id=mc_varint_read(r);if(r->error)return 0;
    if(!id){if(r->n-r->pos<256){r->error=1;return 0;}uint64_t h=signature_hash(r->p+r->pos);r->pos+=256;return h;}
    if(id>MC_CHAT_SIGNATURES){r->error=1;return 0;}
    return c->signatures[id-1];
}
/* MessageSignatureCache.push: newest entries move to the front, displaced unique ones shift back. */
static void push_signatures(McChat *c,const uint64_t *items,unsigned count){
    uint64_t queue[2*MC_CHAT_SIGNATURES+32];unsigned head=MC_CHAT_SIGNATURES+16,tail=head;
    for(unsigned i=0;i<count;i++)if(items[i])queue[tail++]=items[i];
    for(unsigned i=0;tail>head && i<MC_CHAT_SIGNATURES;i++){
        uint64_t old=c->signatures[i];c->signatures[i]=queue[--tail];
        int pushed=0;for(unsigned j=0;j<count;j++)if(items[j]==old)pushed=1;
        if(old && !pushed)queue[--head]=old;
    }
}
static void chat_types(McChat *c,McReader *r){
    c->type_count=0;
    mc_skip(r,7);uint32_t worlds=mc_varint_read(r);if(r->error || worlds>1024)return;
    for(uint32_t i=0;i<worlds && !r->error;i++){uint32_t n=mc_varint_read(r);mc_skip(r,n);}
    McNbt codec,registry,list;
    if(r->error || mc_nbt_root(r,&codec)<0 || mc_nbt_find(codec,"minecraft:chat_type",&registry)<0 || mc_nbt_find(registry,"value",&list)<0 || list.type!=9)return;
    McReader a={list.data,list.size,0,0};unsigned type=mc_u8(&a);uint32_t count=mc_u32(&a);if(type!=10)return;
    for(uint32_t i=0;i<count && c->type_count<MC_CHAT_TYPES && !a.error;i++){
        McNbt entry,id,element,decoration,key,params;McChatType t={0};
        if(mc_nbt_payload(&a,10,0,&entry)<0)break;
        if(mc_nbt_find(entry,"id",&id)<0 || mc_nbt_int(id,&t.id)<0 || mc_nbt_find(entry,"element",&element)<0 || mc_nbt_find(element,"chat",&decoration)<0 ||
           mc_nbt_find(decoration,"translation_key",&key)<0 || mc_nbt_string(key,t.key,sizeof(t.key))<0)continue;
        if(mc_nbt_find(decoration,"parameters",&params)==0 && params.type==9){
            McReader pr={params.data,params.size,0,0};unsigned ptype=mc_u8(&pr);uint32_t pcount=mc_u32(&pr);
            for(uint32_t j=0;ptype==8 && j<pcount && t.count<3 && !pr.error;j++){
                McNbt name;char value[16];if(mc_nbt_payload(&pr,8,0,&name)<0 || mc_nbt_string(name,value,sizeof(value))<0)break;
                t.params[t.count++]=!strcmp(value,"sender")?0:!strcmp(value,"target")?1:2;
            }
        }
        c->types[c->type_count++]=t;
    }
}
static void decorate(const McChat *c,int type,const char *name,const char *target,const char *content,char *out,size_t cap){
    const McChatType *t=NULL;for(unsigned i=0;i<c->type_count;i++)if(c->types[i].id==type)t=&c->types[i];
    const char *format=t?mc_text_translation(t->key):NULL;
    if(!format){if(name[0])snprintf(out,cap,"<%.100s> %.278s",name,content);else snprintf(out,cap,"%s",content);return;}
    const char *args[3]={name,target,content};size_t used=0;unsigned arg=0;
    for(const char *f=format;*f && used+1<cap;){
        if(f[0]=='%' && f[1]=='s'){const char *v=arg<t->count?args[t->params[arg]]:"";arg++;while(*v && used+1<cap)out[used++]=*v++;f+=2;}
        else out[used++]=*f++;
    }
    out[used]=0;
}
static void flatten(char *text){for(;*text;text++)if(*text=='\n')*text=' ';}
int mc_chat_packet(McChat *c,uint32_t id,const uint8_t *p,size_t n,uint64_t now){
    if(id==0x28){mc_chat_init(c);McReader r={p,n,0,0};chat_types(c,&r);return 0;}
    if(id==0x0f){
        McReader r={p,n,0,0};int transaction=(int)mc_varint_read(&r),start=(int)mc_varint_read(&r),length=(int)mc_varint_read(&r);uint32_t count=mc_varint_read(&r);
        if(r.error || count>4096 || start<0 || length<0 || start>32500 || length>32500)return -1;
        char matches[MC_CHAT_SUGGESTIONS][64];unsigned kept=0;
        for(uint32_t i=0;i<count && !r.error;i++){
            char match[64];plain(&r,match,sizeof(match));
            if(boolean(&r)){char tooltip[MC_CHAT_TEXT];component(&r,tooltip,sizeof(tooltip));}
            if(!r.error && kept<MC_CHAT_SUGGESTIONS)memcpy(matches[kept++],match,sizeof(match));
        }
        if(r.error || r.pos!=n)return -1;
        if(transaction!=c->suggest_id)return 0; /* Stale response. */
        c->suggest_start=start;c->suggest_length=length;c->suggest_count=(int)kept;memcpy(c->suggestions,matches,sizeof(matches));c->suggest_updates++;
        return 0;
    }
    if(id!=0x64 && id!=0x46 && id!=0x1b && id!=0x35 && id!=0x19)return 0;
    McReader r={p,n,0,0};char text[MC_CHAT_TEXT]={0},name[128]={0},target[128]={0};int action=id==0x46,signed_message=0,type=-1;
    uint64_t signature=0,seen[21];unsigned seen_count=0;
    if(id==0x19){
        uint64_t h=packed_signature(c,&r);if(r.error || r.pos!=n)return -1;
        for(unsigned i=0;h && i<MC_CHAT_LINES;i++)if(c->lines[i].signature==h){
            snprintf(c->lines[i].text,sizeof(c->lines[i].text),"This chat message has been deleted by the server.");c->lines[i].signature=0;c->deleted++;}
        return 0;
    }
    if(id==0x64 || id==0x46){component(&r,text,sizeof(text));if(id==0x64)action=boolean(&r);}
    else{
        if(id==0x35){
            mc_skip(&r,16);mc_varint_read(&r);signed_message=boolean(&r);
            if(signed_message){if(r.n-r.pos<256)return -1;signature=signature_hash(r.p+r.pos);mc_skip(&r,256);}
            plain(&r,text,sizeof(text));
            mc_u64(&r);mc_u64(&r);unsigned count=mc_varint_read(&r);if(count>20)return -1;
            for(unsigned i=0;i<count && !r.error;i++)seen[seen_count++]=packed_signature(c,&r);
            if(boolean(&r))component(&r,text,sizeof(text));
            unsigned filter=mc_varint_read(&r);if(filter>2)return -1;
            if(filter==2){unsigned words=mc_varint_read(&r);if(words>64)return -1;mc_skip(&r,words*8);}
            /* Do not show text the server marked fully or partially filtered. */
            if(filter)snprintf(text,sizeof(text),"[filtered message]");
        }else component(&r,text,sizeof(text));
        type=(int)mc_varint_read(&r);component(&r,name,sizeof(name));if(boolean(&r))component(&r,target,sizeof(target));
    }
    if(r.error || r.pos!=n)return -1;
    if(id==0x35){seen[seen_count++]=signature;push_signatures(c,seen,seen_count);}
    if(signed_message)c->signed_pending++;
    flatten(text);flatten(name);flatten(target);
    if(action){snprintf(c->action,sizeof(c->action),"%s",text);c->action_at=now;}
    else{
        McChatLine *line=&c->lines[c->next];
        if(type>=0)decorate(c,type,name,target,text,line->text,sizeof(line->text));
        else snprintf(line->text,sizeof(line->text),"%s",text);
        line->at=now;line->signature=signature;c->next=(c->next+1)%MC_CHAT_LINES;if(c->count<MC_CHAT_LINES)c->count++;
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
int mc_chat_suggest_encode(int transaction,const char *text,uint8_t *out,size_t capacity){
    size_t len=strlen(text);if(transaction<0 || !len || len>256 || text[0]!='/' || capacity<len+12)return -1;
    for(size_t i=0;i<len;i++)if((unsigned char)text[i]<32 || (unsigned char)text[i]>126)return -1;
    size_t n=0;out[n++]=0x09;n+=mc_varint_write(out+n,(uint32_t)transaction);n+=mc_varint_write(out+n,(uint32_t)len);memcpy(out+n,text,len);return (int)(n+len);
}
int mc_chat_suggest_apply(const McChat *chat,const char *text,int index,char *out,size_t capacity){
    size_t len=strlen(text);if(index<0 || index>=chat->suggest_count || (size_t)chat->suggest_start>len)return -1;
    size_t end=(size_t)chat->suggest_start+chat->suggest_length;if(end>len)end=len;
    int written=snprintf(out,capacity,"%.*s%s%s",chat->suggest_start,text,chat->suggestions[index],text+end);
    return written<0 || (size_t)written>=capacity?-1:0;
}
