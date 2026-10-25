#ifndef MC_PROTOCOL_H
#define MC_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#define MC_PROTOCOL 762
#define MC_VERSION "1.19.4"
#define MC_MAX_PACKET (2 * 1024 * 1024)
typedef struct { const uint8_t *p; size_t n, pos; int error; } McReader;
size_t mc_varint_write(uint8_t *p, uint32_t n);
uint32_t mc_varint_read(McReader *r);
typedef int (*McPacketHandler)(void *context, uint32_t id, const uint8_t *payload, size_t size);
typedef struct {
    McPacketHandler handler;
    void *handler_context;
    int fd, threshold, play, joined, position_ready, can_fly, flying, gamemode;
    uint64_t movement_packets, abilities_updates;
    uint64_t packets, chunks, keepalives, teleports;
    double x, y, z;
    float yaw, pitch;
    char message[768];
} McClient;
void mc_init(McClient *c);
void mc_close(McClient *c);
int mc_status(McClient *c, const char *ip, uint16_t port, char *json, size_t capacity, unsigned *ping);
int mc_login(McClient *c, const char *ip, uint16_t port, const char *username);
int mc_move(McClient *c,double x,double y,double z,float yaw,float pitch,int grounded);
int mc_set_flying(McClient *c,int flying);
/* Process one packet with a bounded wait. Returns -1 on disconnect/error. */
int mc_step(McClient *c);
#endif
