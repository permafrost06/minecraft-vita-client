#include "book.h"
#include "items.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
/* NBT string payload (u16 length + modified UTF-8) as a bounded raw C string. */
static int raw_string(const uint8_t *data,size_t size,char *out,size_t capacity){
    if(size<2)return -1;
    size_t n=(size_t)data[0]<<8|data[1];if(n!=size-2)return -1;
    size_t copy=n<capacity-1?n:capacity-1;memcpy(out,data+2,copy);out[copy]=0;
    return memchr(out,0,copy)?-1:0;
}
static void field(McNbt tag,const char *key,char *out,size_t capacity){
    McNbt node;char raw[256];out[0]=0;
    if(mc_nbt_find(tag,key,&node)==0 && node.type==8 && raw_string(node.data,node.size,raw,sizeof(raw))==0)mc_text_legacy(raw,out,capacity);
}
int mc_book_read(const McItem *item,McBook *book){
    memset(book,0,sizeof(*book));
    if(!item || !item->stack.count || item->stack.item<0)return -1;
    const char *name=mc_item_def(item->stack.item)->name;
    if(!strcmp(name,"written_book"))book->written=1;else if(strcmp(name,"writable_book"))return -1;
    McNbt tag,list;
    if(mc_item_tag(item,&tag)<0)return 0;
    field(tag,"title",book->title,sizeof(book->title));field(tag,"author",book->author,sizeof(book->author));
    if(mc_nbt_find(tag,"pages",&list)<0 || list.type!=9)return 0;
    McReader r={list.data,list.size,0,0};unsigned type=mc_u8(&r);uint32_t count=mc_u32(&r);
    if(r.error || (count && type!=8))return 0;
    char *raw=malloc(32768);if(!raw)return 0;
    for(uint32_t i=0;i<count && book->count<MC_BOOK_PAGES;i++){
        McNbt page;if(mc_nbt_payload(&r,8,0,&page)<0)break;
        char *out=book->pages[book->count++];
        if(raw_string(page.data,page.size,raw,32768)<0){out[0]=0;continue;}
        /* Written pages are JSON text components; plain strings are accepted too. */
        if(!book->written || mc_text_multiline((const uint8_t *)raw,strlen(raw),out,MC_BOOK_PAGE_TEXT)<0)mc_text_legacy_lines(raw,out,MC_BOOK_PAGE_TEXT);
    }
    free(raw);return 0;
}
static size_t put_string(uint8_t *out,const char *s,size_t n){size_t h=mc_varint_write(out,(uint32_t)n);memcpy(out+h,s,n);return h+n;}
int mc_book_edit_packet(int slot,const McBook *book,const char *title,uint8_t *out,size_t capacity){
    if((slot<0 || slot>8) && slot!=40)return -1;
    if(book->count<0 || book->count>MC_BOOK_PAGES)return -1;
    size_t title_len=title?strlen(title):0;if(title && (!title_len || title_len>16))return -1;
    size_t need=16+title_len;for(int i=0;i<book->count;i++)need+=5+strlen(book->pages[i]);
    if(need>capacity)return -1;
    size_t n=0;out[n++]=0x0e;n+=mc_varint_write(out+n,(uint32_t)slot);n+=mc_varint_write(out+n,(uint32_t)book->count);
    for(int i=0;i<book->count;i++){size_t len=strlen(book->pages[i]);if(len>8192)return -1;n+=put_string(out+n,book->pages[i],len);}
    out[n++]=title?1:0;if(title)n+=put_string(out+n,title,title_len);
    return (int)n;
}
int mc_book_wrap(const char *page,int width,char lines[][64],int max_lines){
    if(width<1 || width>63)return 0;
    int count=0;const char *p=page;
    while(*p && count<max_lines){
        if(*p=='\n'){lines[count++][0]=0;p++;continue;}
        int len=0,space=-1;
        while(p[len] && p[len]!='\n' && len<width){if(p[len]==' ')space=len;len++;}
        int take=len,skip=0;
        if(p[len] && p[len]!='\n' && p[len]!=' ' && space>0){take=space;skip=1;}
        else if(p[len]==' ')skip=1;
        memcpy(lines[count],p,(size_t)take);lines[count][take]=0;count++;
        p+=take+skip;if(*p=='\n')p++;
    }
    return count;
}
