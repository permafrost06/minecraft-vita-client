#ifndef MC_TEXT_H
#define MC_TEXT_H
#include <stdint.h>
#include <stddef.h>
int mc_text_plain(const uint8_t *p,size_t n,char *out,size_t capacity);
#endif
