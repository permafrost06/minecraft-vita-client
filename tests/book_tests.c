#include "book.h"
#include "items.h"
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
static uint8_t wire[4096];static size_t n;
static void b(unsigned v){wire[n++]=(uint8_t)v;}
static void u16(unsigned v){b(v>>8);b(v);}
static void u32(unsigned v){b(v>>24);b(v>>16);b(v>>8);b(v);}
static void nstr(const char *s){u16(strlen(s));memcpy(wire+n,s,strlen(s));n+=strlen(s);}
static void tag(int type,const char *name){b(type);nstr(name);}
static int item_id(const char *name){for(unsigned i=0;i<mc_items_count();i++)if(!strcmp(mc_item_def(i)->name,name))return (int)i;return -1;}
static McItem book_item(const char *kind,const char *const *pages,int count,const char *title){
    n=0;b(1);n+=mc_varint_write(wire+n,(uint32_t)item_id(kind));b(1);b(10);u16(0);
    if(title){tag(8,"title");nstr(title);tag(8,"author");nstr("\xc2\xa7""9Alex");}
    tag(9,"pages");b(8);u32((unsigned)count);for(int i=0;i<count;i++)nstr(pages[i]);
    b(0);
    return (McItem){{item_id(kind),1},wire,n};
}
int main(void){
    mc_mesh_init();static McBook book;
    /* Written book pages are JSON components and keep their line breaks. */
    const char *const written[]={"{\"text\":\"Hello\\nworld\"}","\"Second page\"","not json"};
    McItem item=book_item("written_book",written,3,"Diary");
    CHECK(mc_book_read(&item,&book)==0 && book.written && book.count==3);
    CHECK(!strcmp(book.pages[0],"Hello\nworld") && !strcmp(book.pages[1],"Second page") && !strcmp(book.pages[2],"not json"));
    CHECK(!strcmp(book.title,"Diary") && !strcmp(book.author,"Alex"));
    /* Writable book pages are plain text. */
    const char *const plain[]={"line one\nline two","{\"text\":\"raw\"}"};
    item=book_item("writable_book",plain,2,NULL);
    CHECK(mc_book_read(&item,&book)==0 && !book.written && book.count==2 && !strcmp(book.pages[0],"line one\nline two") && !strcmp(book.pages[1],"{\"text\":\"raw\"}"));
    /* An untagged writable book is empty; other items are not books. */
    McItem empty={{item_id("writable_book"),1},NULL,0};CHECK(mc_book_read(&empty,&book)==0 && book.count==0);
    McItem stone={{1,1},NULL,0};CHECK(mc_book_read(&stone,&book)<0);
    /* Edit Book encoding: slot, pages and optional signing title. */
    item=book_item("writable_book",plain,2,NULL);mc_book_read(&item,&book);
    uint8_t out[512];int size=mc_book_edit_packet(3,&book,"My Book",out,sizeof(out));
    CHECK(size>0 && out[0]==0x0e && out[1]==3 && out[2]==2 && out[3]==17 && !memcmp(out+4,"line one\nline two",17));
    CHECK(out[size-9]==1 && out[size-8]==7 && !memcmp(out+size-7,"My Book",7));
    size=mc_book_edit_packet(40,&book,NULL,out,sizeof(out));CHECK(size>0 && out[1]==40 && out[size-1]==0);
    CHECK(mc_book_edit_packet(9,&book,NULL,out,sizeof(out))<0 && mc_book_edit_packet(0,&book,"this title is too long",out,sizeof(out))<0 && mc_book_edit_packet(0,&book,NULL,out,8)<0);
    /* Word wrapping at spaces, explicit breaks and blank lines. */
    char lines[16][64];int count=mc_book_wrap("The quick brown fox jumps\n\nover",10,lines,16);
    CHECK(count==5 && !strcmp(lines[0],"The quick") && !strcmp(lines[1],"brown fox") && !strcmp(lines[2],"jumps") && !lines[3][0] && !strcmp(lines[4],"over"));
    count=mc_book_wrap("abcdefghijklmnop",8,lines,16);CHECK(count==2 && !strcmp(lines[0],"abcdefgh") && !strcmp(lines[1],"ijklmnop"));
    puts("Book checks passed: written/writable pages, titles, edit/sign packets, wrapping");
    return 0;
}
