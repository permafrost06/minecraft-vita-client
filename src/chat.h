#ifndef MC_CHAT_H
#define MC_CHAT_H
#include "reader.h"
#define MC_CHAT_LINES 32
#define MC_CHAT_TEXT 384
typedef struct {char text[MC_CHAT_TEXT];uint64_t at;} McChatLine;
typedef struct {McChatLine lines[MC_CHAT_LINES];unsigned count,next;char action[MC_CHAT_TEXT];uint64_t action_at;unsigned signed_pending;} McChat;
void mc_chat_init(McChat *chat);
int mc_chat_packet(McChat *chat,uint32_t id,const uint8_t *p,size_t n,uint64_t now);
/* ASCII input, max 256 characters, leading slash selects command encoding. */
int mc_chat_encode(const char *text,uint64_t timestamp,unsigned offset,uint8_t *out,size_t capacity);
#endif
