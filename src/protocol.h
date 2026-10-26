#ifndef MC_PROTOCOL_H
#define MC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define MC_PROTOCOL 762
#define MC_VERSION "1.19.4"
#define MC_MAX_PACKET (2 * 1024 * 1024)
#define MC_BRAND "vitablock"
typedef struct { const uint8_t *p; size_t n, pos; int error; } McReader;
size_t mc_varint_write(uint8_t *p, uint32_t n);
uint32_t mc_varint_read(McReader *r);
typedef int (*McPacketHandler)(void *context, uint32_t id, const uint8_t *payload, size_t size);
typedef struct { int item, count; } McSlot;
typedef struct {
    McPacketHandler handler;
    void *handler_context;
    int fd, threshold, play, joined, position_ready, can_fly, flying, gamemode;
    uint64_t movement_packets, abilities_updates,vehicle_updates;
    double vehicle_position[3];
    float vehicle_yaw,vehicle_pitch;
    int entity_id,health_ready,food,experience_level,experience_total,respawn_pending,immediate_respawn;
    float health,saturation,experience,eye_height;
    int sneaking,sprinting,pose;
    double max_health,attack_speed,movement_speed,armor;
    int bed[3],has_bed;
    int entity_flags,air,using_item,using_offhand;
    float knockback[3],explosion_motion[3];
    uint64_t explosion_updates;
#define MC_COOLDOWNS 16
    int cooldown_item[MC_COOLDOWNS];uint64_t cooldown_start[MC_COOLDOWNS],cooldown_until[MC_COOLDOWNS];
    uint64_t health_updates,death_updates,knockback_updates,using_updates,attribute_updates;
    uint8_t effect_amplifier[64];
    uint64_t effect_until[64];
    char death_message[384];
    McSlot hotbar[9];
    int selected_slot;
    uint32_t interaction_sequence, interaction_ack;
    uint64_t break_packets, place_packets, inventory_updates,dig_finishes,dig_cancels,use_packets,attack_packets;
    uint64_t packets, chunks, keepalives, teleports;
    uint64_t login_plugin_requests, resource_packs_declined, resource_packs_forced;
    /* Server-opened editors: sign position and book hand (0 main, 1 offhand). */
    int sign_editor[3],book_hand;
    uint64_t sign_requests,book_requests;
    double x, y, z;
    float yaw, pitch;
    char message[768];
} McClient;
void mc_init(McClient *c);
void mc_close(McClient *c);
int mc_status(McClient *c, const char *ip, uint16_t port, char *json, size_t capacity, unsigned *ping);
int mc_login(McClient *c, const char *ip, uint16_t port, const char *username);
int mc_move(McClient *c,double x,double y,double z,float yaw,float pitch,int grounded);
int mc_rider_input(McClient *c,float sideways,float forward,int jump,int dismount,float yaw,float pitch);
int mc_vehicle_move(McClient *c,double x,double y,double z,float yaw,float pitch);
int mc_boat_paddles(McClient *c,int left,int right);
int mc_player_command(McClient *c,int action);
int mc_set_movement_flags(McClient *c,int sneak,int sprint);
int mc_set_flying(McClient *c,int flying);
int mc_select_slot(McClient *c,int slot);
/* Assign a plain building item to a creative hotbar slot. */
int mc_creative_slot(McClient *c,int slot,int item);
int mc_creative_item(McClient *c,int slot,int item,int count);
/* Module packet encoders provide the packet ID and validated play payload. */
int mc_send_play(McClient *c,const uint8_t *payload,size_t size);
int mc_block_action(McClient *c,int place,int x,int y,int z,int face,const float cursor[3]);
int mc_dig(McClient *c,int status,int x,int y,int z,int face);
int mc_use_block(McClient *c,int x,int y,int z,int face,const float cursor[3],int hand);
int mc_use_air(McClient *c,int hand);
int mc_release_use(McClient *c);
int mc_swing(McClient *c,int hand);
int mc_entity_action(McClient *c,int entity,int attack,int hand,int sneaking);
int mc_respawn(McClient *c);
/* Update Sign with four lines of at most 384 characters each. */
#define MC_SIGN_PACKET_MAX (1+8+4*(3+384))
int mc_sign_packet(int x,int y,int z,const char *const lines[4],uint8_t *out,size_t capacity);
int mc_update_sign(McClient *c,int x,int y,int z,const char *const lines[4]);
/* Remaining fraction [0,1] of a server item cooldown; 0 when the item is usable. */
float mc_item_cooldown(const McClient *c,int item,uint64_t now);
/* Process one packet with a bounded wait. Returns -1 on disconnect/error. */
int mc_step(McClient *c);
#endif
