#ifndef MC_TEXT_H
#define MC_TEXT_H
#include <stdint.h>
#include <stddef.h>
int mc_text_plain(const uint8_t *p,size_t n,char *out,size_t capacity);
/* Plain wire string to ASCII, dropping legacy section-sign formatting codes. */
void mc_text_legacy(const char *in,char *out,size_t capacity);
/* Variants that keep line breaks (books, kick screens). */
int mc_text_multiline(const uint8_t *p,size_t n,char *out,size_t capacity);
void mc_text_legacy_lines(const char *in,char *out,size_t capacity);
/* Built-in English format for a translation key (%s placeholders), or NULL. */
const char *mc_text_translation(const char *key);
#endif
