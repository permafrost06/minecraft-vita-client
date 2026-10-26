#ifndef MC_CHAT_H
#define MC_CHAT_H
#include "reader.h"
#define MC_CHAT_LINES 32
#define MC_CHAT_TEXT 384
typedef struct {char text[MC_CHAT_TEXT];uint64_t at,signature;} McChatLine;
#define MC_CHAT_TYPES 32
#define MC_CHAT_SIGNATURES 128
/* Chat type decoration from the Join Game registry; parameters: 0 sender, 1 target, 2 content. */
typedef struct {int id;char key[64];uint8_t params[3],count;} McChatType;
typedef struct {
    McChatLine lines[MC_CHAT_LINES];unsigned count,next;char action[MC_CHAT_TEXT];uint64_t action_at;unsigned signed_pending;
    McChatType types[MC_CHAT_TYPES];unsigned type_count;
    /* Vanilla's last-seen signature cache (hashes) resolves packed signature references. */
    uint64_t signatures[MC_CHAT_SIGNATURES];
    uint64_t deleted;
    /* Latest command suggestions: replace text[start, start+length) with a match. */
#define MC_CHAT_SUGGESTIONS 16
    int suggest_id,suggest_start,suggest_length,suggest_count;char suggestions[MC_CHAT_SUGGESTIONS][64];uint64_t suggest_updates;
} McChat;
void mc_chat_init(McChat *chat);
int mc_chat_packet(McChat *chat,uint32_t id,const uint8_t *p,size_t n,uint64_t now);
/* ASCII input, max 256 characters, leading slash selects command encoding. */
int mc_chat_encode(const char *text,uint64_t timestamp,unsigned offset,uint8_t *out,size_t capacity);
/* Command Suggestions Request for text beginning with '/'. */
int mc_chat_suggest_encode(int transaction,const char *text,uint8_t *out,size_t capacity);
/* Applies suggestion `index` to `text` (as sent in the request) into out. */
int mc_chat_suggest_apply(const McChat *chat,const char *text,int index,char *out,size_t capacity);
#endif
