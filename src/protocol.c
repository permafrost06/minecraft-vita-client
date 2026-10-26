#include "protocol.h"
#include "net.h"
#include "nbt.h"
#include "reader.h"
#include "items.h"
#include "entities.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
size_t mc_varint_write(uint8_t *p, uint32_t v) {
    size_t n = 0;
    do { uint8_t b = v & 127; v >>= 7; p[n++] = b | (v ? 128 : 0); } while (v);
    return n;
}
uint32_t mc_varint_read(McReader *r) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 5; i++) {
        if (r->pos >= r->n) break;
        uint8_t b = r->p[r->pos++];
        if (i == 4 && (b & 0xf0)) break;
        v |= (uint32_t)(b & 127) << (7*i);
        if (!(b & 128)) return v;
    }
    r->error = 1; return 0;
}
static int fail(McClient *c, const char *message) {
    snprintf(c->message, sizeof(c->message), "%s", message);
    mc_close(c); return -1;
}
void mc_init(McClient *c) { memset(c, 0, sizeof(*c)); c->fd = -1; c->threshold = -1;c->entity_id=-1;c->health=20;c->food=20;c->max_health=20;c->attack_speed=4;c->movement_speed=.1;c->air=300;c->eye_height=1.62f; for(int i=0;i<9;i++)c->hotbar[i].item=-1; }
void mc_close(McClient *c) { net_close(c->fd); c->fd = -1; c->play = 0; }
static int send_packet(McClient *c, const uint8_t *p, size_t n) {
    uint8_t prefix[10]; size_t plen = 0;
    uint8_t *encoded = NULL;
    const uint8_t *data = p; size_t len = n;
    if (c->threshold >= 0) {
        if (n >= (size_t)c->threshold) {
            uLongf compressed = compressBound(n);
            encoded = malloc(compressed);
            if (!encoded) return fail(c, "Out of memory encoding packet");
            if (compress2(encoded, &compressed, p, n, Z_DEFAULT_COMPRESSION) != Z_OK) {
                free(encoded); return fail(c, "Compression failed");
            }
            data = encoded; len = compressed;
            plen = mc_varint_write(prefix+5, n);
        } else plen = mc_varint_write(prefix+5, 0);
    }
    size_t head = mc_varint_write(prefix, len+plen);
    uint64_t deadline = net_millis()+5000;
    int r = net_write(c->fd, prefix, head, deadline);
    if (!r && plen) r = net_write(c->fd, prefix+5, plen, deadline);
    if (!r) r = net_write(c->fd, data, len, deadline);
    free(encoded);
    return r < 0 ? fail(c, "Send failed or timed out") : 0;
}
static int recv_packet(McClient *c, uint8_t **data, size_t *size) {
    uint8_t header[5]; size_t h = 0;
    uint64_t deadline = net_millis()+5000;
    do {
        if (h == 5) return fail(c, "Invalid packet length VarInt");
        if (net_read(c->fd, header+h, 1, deadline) < 0) return fail(c, "Connection closed or receive timed out");
    } while (header[h++] & 128);
    McReader reader = {header, h, 0, 0};
    uint32_t n = mc_varint_read(&reader);
    if (reader.error || !n || n > MC_MAX_PACKET) return fail(c, "Invalid or oversized packet");
    uint8_t *raw = malloc(n);
    if (!raw) return fail(c, "Out of memory receiving packet");
    if (net_read(c->fd, raw, n, deadline) < 0) { free(raw); return fail(c, "Truncated packet or receive timeout"); }
    if (c->threshold >= 0) {
        reader = (McReader){raw, n, 0, 0};
        uint32_t unpacked = mc_varint_read(&reader);
        if (reader.error || unpacked > MC_MAX_PACKET || reader.pos == n) {
            free(raw); return fail(c, "Invalid compressed packet length");
        }
        if (unpacked) {
            if (unpacked < (uint32_t)c->threshold) { free(raw); return fail(c, "Compressed packet below threshold"); }
            uint8_t *decoded = malloc(unpacked);
            if (!decoded) { free(raw); return fail(c, "Out of memory decompressing packet"); }
            z_stream stream; memset(&stream, 0, sizeof(stream));
            stream.next_in = raw+reader.pos; stream.avail_in = n-reader.pos;
            stream.next_out = decoded; stream.avail_out = unpacked;
            int r = inflateInit(&stream);
            if (r == Z_OK) {
                r = inflate(&stream, Z_FINISH);
                int valid = r == Z_STREAM_END && stream.total_out == unpacked && stream.avail_in == 0;
                inflateEnd(&stream);
                r = valid ? Z_OK : Z_DATA_ERROR;
            }
            free(raw);
            if (r != Z_OK) { free(decoded); return fail(c, "Invalid zlib packet"); }
            raw = decoded; n = unpacked;
        } else {
            n -= reader.pos;
            if (n >= (uint32_t)c->threshold) { free(raw); return fail(c, "Uncompressed packet exceeds threshold"); }
            memmove(raw, raw+reader.pos, n);
        }
    }
    *data = raw; *size = n; c->packets++; return 0;
}
static size_t put_string(uint8_t *p, const char *s) {
    size_t n = strlen(s), h = mc_varint_write(p, n); memcpy(p+h, s, n); return h+n;
}
static int get_string(McReader *r, char *out, size_t capacity) {
    uint32_t n = mc_varint_read(r);
    if (r->error || n > r->n-r->pos || !capacity) { r->error = 1; return -1; }
    size_t copy = n < capacity-1 ? n : capacity-1;
    /* Strip controls before text reaches a terminal/debug-screen renderer. */
    for (size_t i = 0; i < copy; i++) out[i] = r->p[r->pos+i] < 32 ? ' ' : r->p[r->pos+i];
    out[copy] = 0; r->pos += n; return 0;
}
static int skip_string(McReader *r) {
    uint32_t n = mc_varint_read(r);
    if (r->error || n > r->n-r->pos) return -1;
    r->pos += n; return 0;
}
static int read_properties(McReader *r) {
    uint32_t count = mc_varint_read(r);
    if (r->error || count > 1024) return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (skip_string(r) < 0 || skip_string(r) < 0 || r->pos >= r->n) return -1;
        uint8_t signed_property = r->p[r->pos++];
        if (signed_property > 1 || (signed_property && skip_string(r) < 0)) return -1;
    }
    return 0;
}
static int connect_state(McClient *c, const char *ip, uint16_t port, int state) {
    mc_close(c); mc_init(c);
    if (!port || strlen(ip) > 255) return fail(c, "Invalid server address");
    c->fd = net_connect(ip, port);
    if (c->fd < 0) return fail(c, "Cannot connect (IPv4 only); check Wi-Fi, address and port");
    uint8_t p[300]; size_t n = 0;
    p[n++] = 0; n += mc_varint_write(p+n, MC_PROTOCOL); n += put_string(p+n, ip);
    p[n++] = port >> 8; p[n++] = port & 255; p[n++] = state;
    return send_packet(c, p, n);
}
int mc_status(McClient *c, const char *ip, uint16_t port, char *json, size_t capacity, unsigned *ping) {
    if (connect_state(c, ip, port, 1) < 0) return -1;
    uint8_t request = 0, *p; size_t n;
    if (send_packet(c, &request, 1) < 0 || recv_packet(c, &p, &n) < 0) return -1;
    McReader r = {p, n, 0, 0}; uint32_t id = mc_varint_read(&r);
    int ok = id == 0 && get_string(&r, json, capacity) == 0 && r.pos == n;
    free(p);
    if (!ok) return fail(c, "Invalid server status response");
    uint8_t echo[9] = {1}; uint64_t start = net_millis();
    for (int i = 0; i < 8; i++) echo[8-i] = (uint8_t)(start >> (i*8));
    if (send_packet(c, echo, sizeof(echo)) < 0 || recv_packet(c, &p, &n) < 0) return -1;
    ok = n == sizeof(echo) && memcmp(echo, p, n) == 0; free(p);
    if (!ok) return fail(c, "Server returned an invalid pong");
    *ping = (unsigned)(net_millis()-start);
    snprintf(c->message, sizeof(c->message), "Status and ping OK (%u ms)", *ping);
    mc_close(c); return 0;
}
int mc_login(McClient *c, const char *ip, uint16_t port, const char *username) {
    size_t len = strlen(username);
    if (!len || len > 16) return fail(c, "Username must contain 1-16 letters, digits or underscores");
    for (size_t i = 0; i < len; i++) {
        char b = username[i];
        if (!((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'))
            return fail(c, "Invalid username");
    }
    if (connect_state(c, ip, port, 2) < 0) return -1;
    uint8_t p[32] = {0}; size_t n = 1+put_string(p+1, username);
    p[n++] = 0; /* No optional player UUID; offline-mode server derives it. */
    if (send_packet(c, p, n) < 0) return -1;
    snprintf(c->message, sizeof(c->message), "Login requested as %s", username); return 0;
}
static double get_double(McReader *r) {
    if (r->n-r->pos < 8) { r->error = 1; return 0; }
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) bits = (bits << 8) | r->p[r->pos++];
    double value; memcpy(&value, &bits, 8); return value;
}
static float get_float(McReader *r) {
    if (r->n-r->pos < 4) { r->error = 1; return 0; }
    uint32_t bits = 0;
    for (int i = 0; i < 4; i++) bits = (bits << 8) | r->p[r->pos++];
    float value; memcpy(&value, &bits, 4); return value;
}
static void put_number(uint8_t *p, const void *value, size_t n) {
    uint64_t bits = 0;
    if (n == 4) { uint32_t small; memcpy(&small, value, 4); bits = small; }
    else memcpy(&bits, value, 8);
    for (size_t i = 0; i < n; i++) p[n-1-i] = (uint8_t)(bits >> (i*8));
}
int mc_move(McClient *c,double x,double y,double z,float yaw,float pitch,int grounded) {
    if (!c->play || !c->joined || !c->position_ready || !isfinite(x) || !isfinite(y) || !isfinite(z) ||
        !isfinite(yaw) || !isfinite(pitch) || fabs(x)>29999984 || fabs(z)>29999984 || fabs(y)>30000000 || fabsf(pitch)>90) return -1;
    uint8_t p[34]={0x15};
    put_number(p+1,&x,8);put_number(p+9,&y,8);put_number(p+17,&z,8);
    put_number(p+25,&yaw,4);put_number(p+29,&pitch,4);p[33]=grounded?1:0;
    if(send_packet(c,p,sizeof(p))<0)return -1;
    c->x=x;c->y=y;c->z=z;c->yaw=yaw;c->pitch=pitch;c->movement_packets++;return 0;
}
int mc_player_command(McClient *c,int action){
    if(!c->play || !c->joined || action<0 || action>8)return -1;
    uint8_t p[16];size_t n=0;p[n++]=0x1e;n+=mc_varint_write(p+n,c->entity_id);n+=mc_varint_write(p+n,action);p[n++]=0;
    return mc_send_play(c,p,n);
}
int mc_set_movement_flags(McClient *c,int sneak,int sprint){
    sneak=!!sneak;sprint=!!sprint;
    if(sneak!=c->sneaking){if(mc_player_command(c,sneak?0:1)<0)return -1;c->sneaking=sneak;}
    if(sprint!=c->sprinting){if(mc_player_command(c,sprint?3:4)<0)return -1;c->sprinting=sprint;}
    return 0;
}
int mc_set_flying(McClient *c,int flying) {
    if(!c->play || !c->joined || (flying && !c->can_fly))return -1;
    uint8_t p[2]={0x1c,flying?2:0};
    if(send_packet(c,p,2)<0)return -1;
    c->flying=!!flying;return 0;
}
int mc_select_slot(McClient *c,int slot) {
    if(!c->play || !c->joined || slot<0 || slot>8)return -1;
    uint8_t p[3]={0x28,0,(uint8_t)slot};
    if(send_packet(c,p,sizeof(p))<0)return -1;
    c->selected_slot=slot;return 0;
}
int mc_creative_slot(McClient *c,int slot,int item) { return mc_creative_item(c,slot,item,mc_item_def(item)->stack_size); }
int mc_send_play(McClient *c,const uint8_t *payload,size_t size){
    if(!c->play || !c->joined || !size || size>MC_MAX_PACKET)return -1;
    return send_packet(c,payload,size);
}
int mc_creative_item(McClient *c,int slot,int item,int count) {
    if(!c->play || !c->joined || c->gamemode!=1 || slot<0 || slot>8 || item<0 || (item==0 && count>0) || (unsigned)item>=mc_items_count() || count<0 || count>mc_item_def(item)->stack_size)return -1;
    uint8_t p[16]={0x2b,0,(uint8_t)(36+slot),1};
    size_t n=4;
    if(count){n+=mc_varint_write(p+n,item);p[n++]=count;p[n++]=0;}else {p[3]=0;}
    if(send_packet(c,p,n)<0)return -1;
    /* Vanilla does not echo accepted creative inventory writes. Later server
       slot/window updates always replace this locally requested stack. */
    c->hotbar[slot]=(McSlot){count?item:-1,count};return 0;
}
static int interaction_ready(const McClient *c){return c->play && c->joined && c->position_ready && c->gamemode!=3 && (!c->health_ready || c->health>0);}
static int block_position(int x,int y,int z,int face){return x>=-29999984 && x<=29999984 && z>=-29999984 && z<=29999984 && y>=-2048 && y<=2047 && face>=0 && face<=5;}
static void put_position(uint8_t *p,int x,int y,int z){
    uint64_t packed=((uint64_t)((uint32_t)x&0x3ffffff)<<38)|((uint64_t)((uint32_t)z&0x3ffffff)<<12)|((uint32_t)y&0xfff);
    for(int i=0;i<8;i++)p[i]=(uint8_t)(packed>>(56-i*8));
}
int mc_swing(McClient *c,int hand){
    if(!interaction_ready(c) || hand<0 || hand>1)return -1;
    uint8_t p[2]={0x2f,(uint8_t)hand};return send_packet(c,p,2);
}
int mc_dig(McClient *c,int status,int x,int y,int z,int face){
    if(!interaction_ready(c) || status<0 || status>2 || !block_position(x,y,z,face) || c->interaction_sequence==INT32_MAX)return -1;
    uint8_t p[16]={0x1d,(uint8_t)status};put_position(p+2,x,y,z);p[10]=face;
    uint32_t sequence=c->interaction_sequence+1;size_t n=11+mc_varint_write(p+11,sequence);
    if(send_packet(c,p,n)<0)return -1;
    c->interaction_sequence=sequence;
    if(!status)c->break_packets++;else if(status==1)c->dig_cancels++;else c->dig_finishes++;
    return 0;
}
int mc_use_block(McClient *c,int x,int y,int z,int face,const float cursor[3],int hand){
    if(!interaction_ready(c) || hand<0 || hand>1 || !cursor || !block_position(x,y,z,face) || c->interaction_sequence==INT32_MAX)return -1;
    for(int i=0;i<3;i++)if(!isfinite(cursor[i]) || cursor[i]<0 || cursor[i]>1)return -1;
    uint8_t p[32]={0x31,(uint8_t)hand};put_position(p+2,x,y,z);p[10]=face;
    for(int i=0;i<3;i++)put_number(p+11+i*4,&cursor[i],4);
    p[23]=0;
    uint32_t sequence=c->interaction_sequence+1;size_t n=24+mc_varint_write(p+24,sequence);
    if(send_packet(c,p,n)<0)return -1;
    c->interaction_sequence=sequence;c->place_packets++;return 0;
}
int mc_use_air(McClient *c,int hand){
    if(!interaction_ready(c) || hand<0 || hand>1 || c->interaction_sequence==INT32_MAX)return -1;
    uint8_t p[8]={0x32,(uint8_t)hand};uint32_t sequence=c->interaction_sequence+1;size_t n=2+mc_varint_write(p+2,sequence);
    if(send_packet(c,p,n)<0)return -1;
    c->interaction_sequence=sequence;c->use_packets++;return 0;
}
int mc_release_use(McClient *c){
    if(!c->play || !c->joined)return -1;
    uint8_t p[12]={0x1d,5};return send_packet(c,p,sizeof(p));
}
int mc_entity_action(McClient *c,int entity,int attack,int hand,int sneaking){
    if(!interaction_ready(c) || entity<0 || hand<0 || hand>1)return -1;
    uint8_t p[12]={0x10};size_t n=1+mc_varint_write(p+1,entity);p[n++]=attack?1:0;if(!attack)p[n++]=hand;p[n++]=!!sneaking;
    if(send_packet(c,p,n)<0)return -1;
    if(attack)c->attack_packets++;
    return 0;
}
int mc_respawn(McClient *c){
    if(!c->play || !c->joined || !c->health_ready || c->health>0 || c->respawn_pending)return -1;
    uint8_t p[2]={7,0};if(send_packet(c,p,2)<0)return -1;
    c->respawn_pending=1;return 0;
}
int mc_block_action(McClient *c,int place,int x,int y,int z,int face,const float cursor[3]){
    if(c->gamemode!=1)return -1;
    int result=place?mc_use_block(c,x,y,z,face,cursor,0):mc_dig(c,0,x,y,z,face);
    return result<0?result:mc_swing(c,0);
}
static int inventory_packet(McClient *c,uint32_t id,McReader *r) {
    McSlot slots[9];memcpy(slots,c->hotbar,sizeof(slots));
    int window=(int8_t)mc_u8(r);mc_varint_read(r);
    if(id==0x12){
        uint32_t count=mc_varint_read(r);
        if(count>1024)return -1;
        for(uint32_t i=0;i<count && !r->error;i++){
            McSlot slot=mc_read_slot(r);if(window==0 && i>=36 && i<45)slots[i-36]=slot;
        }
        mc_read_slot(r); /* carried stack */
    }else{
        int slot=(int16_t)mc_u16(r);McSlot value=mc_read_slot(r);
        if(window==0 && slot>=36 && slot<45)slots[slot-36]=value;
        /* Window -2 addresses the player inventory directly (hotbar 0..8). */
        else if(window==-2 && slot>=0 && slot<9)slots[slot]=value;
    }
    if(r->error || r->pos!=r->n)return -1;
    memcpy(c->hotbar,slots,sizeof(slots));c->inventory_updates++;return 0;
}
static int attributes_packet(McClient *c,McReader *r){
    int entity=(int32_t)mc_varint_read(r);uint32_t count=mc_varint_read(r);
    if(count>1024)return -1;
    double max_health=c->max_health,attack_speed=c->attack_speed,movement_speed=c->movement_speed,armor=c->armor;
    for(uint32_t i=0;i<count && !r->error;i++){
        char name[256];mc_string(r,name,sizeof(name));double base=get_double(r),add=0,base_mul=0,multiply=1;
        uint32_t modifiers=mc_varint_read(r);if(modifiers>1024)return -1;
        for(uint32_t j=0;j<modifiers && !r->error;j++){
            static const uint8_t sprint_uuid[16]={0x66,0x2a,0x6b,0x8d,0xda,0x3e,0x4c,0x1c,0x88,0x13,0x96,0xea,0x60,0x97,0x27,0x8d};
            int sprint=r->n-r->pos>=16 && !memcmp(r->p+r->pos,sprint_uuid,16) && strstr(name,"movement_speed");
            mc_skip(r,16);double amount=get_double(r);unsigned operation=mc_u8(r);
            if(!isfinite(amount) || operation>2)r->error=1;
            else if(sprint)continue;
            else if(!operation)add+=amount;else if(operation==1)base_mul+=amount;else multiply*=1+amount;
        }
        double value=(base+add)*(1+base_mul)*multiply;
        if(!isfinite(base) || !isfinite(value))r->error=1;
        if(!strcmp(name,"minecraft:generic.max_health") || !strcmp(name,"generic.max_health"))max_health=fmax(1,fmin(1024,value));
        else if(!strcmp(name,"minecraft:generic.attack_speed") || !strcmp(name,"generic.attack_speed"))attack_speed=fmax(0,fmin(1024,value));
        else if(!strcmp(name,"minecraft:generic.movement_speed") || !strcmp(name,"generic.movement_speed"))movement_speed=fmax(0,fmin(1024,value));
        else if(!strcmp(name,"minecraft:generic.armor") || !strcmp(name,"generic.armor"))armor=fmax(0,fmin(30,value));
    }
    if(r->error || r->pos!=r->n)return -1;
    if(entity==c->entity_id){c->max_health=max_health;c->attack_speed=attack_speed;c->movement_speed=movement_speed;c->armor=armor;c->attribute_updates++;}
    return 0;
}
static int effect_packet(McClient *c,McReader *r,int remove){
    int entity=(int32_t)mc_varint_read(r);unsigned effect=mc_varint_read(r),amplifier=0;int32_t duration=0;
    if(!remove){
        amplifier=mc_u8(r);duration=(int32_t)mc_varint_read(r);mc_u8(r);unsigned factor=mc_u8(r);
        if(factor>1 || duration< -1)r->error=1;else if(factor)mc_nbt_root(r,NULL);
    }
    if(r->error || r->pos!=r->n)return -1;
    if(entity==c->entity_id && effect<64){c->effect_amplifier[effect]=amplifier;c->effect_until[effect]=remove?0:duration<0?UINT64_MAX:net_millis()+(uint64_t)duration*50;}
    return 0;
}
int mc_step(McClient *c) {
    uint8_t *p; size_t n;
    if (c->fd < 0) return -1;
    if (!net_ready(c->fd, 20)) return 0;
    if (recv_packet(c, &p, &n) < 0) return -1;
    McReader r = {p, n, 0, 0}; uint32_t id = mc_varint_read(&r);
    int result = 0;
    if (r.error) { free(p); return fail(c, "Missing packet ID"); }
    if (c->play && c->handler && c->handler(c->handler_context,id,p+r.pos,n-r.pos) < 0) {
        free(p); return fail(c, "World packet rejected; see terrain diagnostics");
    }
    if ((!c->play && id == 0) || (c->play && id == 0x1a)) {
        char reason[700];
        if (get_string(&r, reason, sizeof(reason)) < 0) result = fail(c, "Invalid disconnect reason");
        else { snprintf(c->message, sizeof(c->message), "Disconnected: %s", reason); mc_close(c); result = -1; }
    } else if (!c->play) {
        if (id == 1) result = fail(c, "Server requires Microsoft authentication; use a private online-mode=false test server for now");
        else if (id == 3) {
            uint32_t threshold = mc_varint_read(&r);
            if (r.error || r.pos != n || threshold > MC_MAX_PACKET) result = fail(c, "Invalid compression threshold");
            else c->threshold = (int)threshold;
        } else if (id == 2) {
            /* UUID, username and bounded profile properties. */
            char username[17];
            if (n-r.pos < 16) result = fail(c, "Invalid login success");
            else {
                r.pos += 16;
                if (get_string(&r, username, sizeof(username)) < 0 || read_properties(&r) < 0 || r.pos != n) result = fail(c, "Invalid login username");
                else {
                    c->play = 1;
                    snprintf(c->message, sizeof(c->message), "Logged in as %s; waiting for spawn", username);
                }
            }
        } else result = fail(c, "Unexpected login packet (wrong server version?)");
    } else if (id == 0x28) {
        c->joined = 1;
        if (n-r.pos >= 7) {McReader join=r;c->entity_id=(int32_t)mc_u32(&join);c->gamemode=p[r.pos+5];}
        /* Wait for Join Game before sending play packets. Login Success can
           arrive before vanilla's receiving connection has switched state. */
        uint8_t settings[] = {0x08, 5, 'e','n','_','u','s', 2, 0, 1, 0x7f, 1, 0, 1};
        result = send_packet(c, settings, sizeof(settings));
        if (!result) snprintf(c->message, sizeof(c->message), "Joined server. Waiting for nearby terrain.");
    } else if (id == 0x41) {
        if(c->respawn_pending || (c->health_ready && c->health<=0))memset(c->effect_until,0,sizeof(c->effect_until));
        c->position_ready=0;c->flying=0;c->health_ready=0;c->respawn_pending=0;
        if(skip_string(&r)<0 || skip_string(&r)<0 || n-r.pos<9)result=fail(c,"Invalid respawn");
        else {r.pos+=8;c->gamemode=mc_u8(&r);}
    } else if (id == 0x57) {
        float health=get_float(&r);unsigned food=mc_varint_read(&r);float saturation=get_float(&r);
        if(r.error || r.pos!=n || !isfinite(health) || !isfinite(saturation) || food>20 || saturation<0)result=fail(c,"Invalid health update");
        else {c->health=health;c->food=food;c->saturation=saturation;c->health_ready=1;c->health_updates++;}
    } else if (id == 0x56) {
        float bar=get_float(&r);uint32_t level=mc_varint_read(&r),total=mc_varint_read(&r);
        if(r.error || r.pos!=n || !isfinite(bar) || bar<0 || bar>1 || level>INT32_MAX || total>INT32_MAX)result=fail(c,"Invalid experience update");
        else {c->experience=bar;c->experience_level=level;c->experience_total=total;}
    } else if (id == 0x38) {
        int player=(int32_t)mc_varint_read(&r);mc_u32(&r);char message[384];
        if(get_string(&r,message,sizeof(message))<0 || r.error || r.pos!=n)result=fail(c,"Invalid death event");
        else if(player==c->entity_id){c->health=0;c->health_ready=1;c->death_updates++;snprintf(c->death_message,sizeof(c->death_message),"%s",message);}
    } else if (id == 0x54) {
        int entity=(int32_t)mc_varint_read(&r);float velocity[3];for(int i=0;i<3;i++)velocity[i]=(int16_t)mc_u16(&r)/8000.0f;
        if(r.error || r.pos!=n)result=fail(c,"Invalid entity velocity");
        else if(entity==c->entity_id){memcpy(c->knockback,velocity,sizeof(velocity));c->knockback_updates++;}
    } else if (id == 0x6a) {
        if(attributes_packet(c,&r)<0)result=fail(c,"Invalid attributes update");
    } else if (id == 0x6c || id == 0x3f) {
        if(effect_packet(c,&r,id==0x3f)<0)result=fail(c,"Invalid status effect");
    } else if (id == 0x52) {
        int entity=(int32_t)mc_varint_read(&r);
        if(entity==c->entity_id){
            McEntity self={.type=MC_ENTITY_PLAYER,.pose=c->pose,.flags=c->entity_flags,.air=c->air,.using_item=c->using_item,.using_offhand=c->using_offhand};
            if(mc_entity_read_metadata(&r,&self)<0 || r.pos!=n)result=fail(c,"Invalid player metadata");
            else {if(self.using_item!=c->using_item || self.using_offhand!=c->using_offhand)c->using_updates++;c->using_item=self.using_item;c->using_offhand=self.using_offhand;c->air=self.air;c->entity_flags=self.flags;c->pose=self.pose;}
        }
    } else if (id == 0x12 || id == 0x14) {
        if(inventory_packet(c,id,&r)<0)result=fail(c,"Invalid inventory packet");
    } else if (id == 0x4d) {
        unsigned slot=mc_u8(&r);
        if(r.error || r.pos!=n || slot>8)result=fail(c,"Invalid held slot");
        else c->selected_slot=slot;
    } else if (id == 0x06) {
        uint32_t sequence=mc_varint_read(&r);
        if(r.error || r.pos!=n || sequence>c->interaction_sequence)result=fail(c,"Invalid block acknowledgement");
        else if(sequence>c->interaction_ack)c->interaction_ack=sequence;
    } else if (id == 0x1f) {
        unsigned reason=mc_u8(&r);float value=get_float(&r);
        if(r.error || r.pos!=n || !isfinite(value))result=fail(c,"Invalid game event");
        else if(reason==3 && value>=0 && value<=3)c->gamemode=(int)value;
        else if(reason==11)c->immediate_respawn=value!=0;
    } else if (id == 0x34) {
        if (n-r.pos != 9) result=fail(c,"Invalid player abilities");
        else { uint8_t flags=p[r.pos];c->can_fly=!!(flags&4);c->flying=!!(flags&2);c->abilities_updates++; }
    } else if (id == 0x24) c->chunks++;
    else if (id == 0x23) {
        if (n-r.pos != 8) result = fail(c, "Invalid keepalive");
        else { uint8_t reply[9] = {0x12}; memcpy(reply+1, p+r.pos, 8); result = send_packet(c, reply, 9); c->keepalives++; }
    } else if (id == 0x32) {
        if (n-r.pos != 4) result = fail(c, "Invalid play ping");
        else { uint8_t pong[5] = {0x20}; memcpy(pong+1, p+r.pos, 4); result = send_packet(c, pong, 5); }
    } else if (id == 0x3c) {
        double x = get_double(&r), y = get_double(&r), z = get_double(&r);
        float yaw = get_float(&r), pitch = get_float(&r);
        uint8_t flags = 0;
        if (r.pos >= n) r.error = 1; else flags = p[r.pos++];
        uint32_t teleport = mc_varint_read(&r);
        if (r.error || r.pos != n || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(yaw) || !isfinite(pitch)) result = fail(c, "Invalid teleport packet");
        else {
            c->x = x + ((flags & 1) ? c->x : 0); c->y = y + ((flags & 2) ? c->y : 0); c->z = z + ((flags & 4) ? c->z : 0);
            c->yaw = yaw + ((flags & 8) ? c->yaw : 0); c->pitch = pitch + ((flags & 16) ? c->pitch : 0);
            uint8_t reply[40] = {0}; size_t size = 1+mc_varint_write(reply+1, teleport);
            result = send_packet(c, reply, size);
            if (!result) {
                reply[0] = 0x15;
                put_number(reply+1, &c->x, 8); put_number(reply+9, &c->y, 8); put_number(reply+17, &c->z, 8);
                put_number(reply+25, &c->yaw, 4); put_number(reply+29, &c->pitch, 4); reply[33] = 0;
                result = send_packet(c, reply, 34); c->teleports++; c->position_ready=1;
            }
        }
    }
    free(p); return result < 0 ? -1 : 1;
}
