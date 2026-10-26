#ifndef MC_BOOK_H
#define MC_BOOK_H
#include "inventory.h"
/* Written and writable book pages as plain ASCII. */
#define MC_BOOK_PAGES 100
#define MC_BOOK_PAGE_TEXT 800
typedef struct {
    int written,count;
    char title[64],author[32];
    char pages[MC_BOOK_PAGES][MC_BOOK_PAGE_TEXT];
} McBook;
/* Reads a written_book (JSON pages) or writable_book (plain pages). Returns -1
   for other items. An item without pages yields an empty book. */
int mc_book_read(const McItem *item,McBook *book);
/* Edit Book (0x0E): slot is a hotbar index 0-8 or 40 for the offhand. A title
   signs the book. Returns the encoded size or -1. */
int mc_book_edit_packet(int slot,const McBook *book,const char *title,uint8_t *out,size_t capacity);
/* Wraps one page into display lines of at most `width` characters. */
int mc_book_wrap(const char *page,int width,char lines[][64],int max_lines);
#endif
