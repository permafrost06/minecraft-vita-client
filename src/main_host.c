#include "protocol.h"
#include "net.h"
#include "world.h"
#include "render.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int world_packet(void *context,uint32_t id,const uint8_t *p,size_t n) {
    McWorld *w = context; int r = mc_world_packet(w,id,p,n);
    if (r < 0) fprintf(stderr,"World: %s\n",w->error);
    return r;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "Usage: %s status|login|view|walk IPv4 [port] [username] [seconds]\n", argv[0]); return 2; }
    char *end; long port = argc > 3 ? strtol(argv[3], &end, 10) : 25565;
    if (port < 1 || port > 65535 || (argc > 3 && *end)) return 2;
    if (net_init() < 0) return 1;
    McClient c; mc_init(&c); int result = 0;
    if (!strcmp(argv[1], "status")) {
        char *json = malloc(MC_MAX_PACKET+1); unsigned ping;
        if (!json) { net_term(); return 1; }
        result = mc_status(&c, argv[2], (uint16_t)port, json, MC_MAX_PACKET+1, &ping);
        if (!result) printf("%s\n", json);
        free(json);
    } else if (!strcmp(argv[1], "login") || !strcmp(argv[1], "view") || !strcmp(argv[1],"walk")) {
        McWorld world; mc_world_init(&world); int walk = !strcmp(argv[1],"walk"), view = walk || !strcmp(argv[1],"view");
        McPlayer player = {0};uint64_t teleport=0,next_tick=0;
        result = mc_login(&c, argv[2], (uint16_t)port, argc > 4 ? argv[4] : "VitaPlayer");
        if (view) { c.handler = world_packet; c.handler_context = &world; }
        uint64_t end_time = net_millis()+(uint64_t)(argc > 5 ? atoi(argv[5]) : 40)*1000;
        while (!result && net_millis() < end_time) {
            if (mc_step(&c) < 0) result = -1;
            if (walk && c.position_ready && c.teleports != teleport) {
                mc_player_reset(&player,c.x,c.y,c.z,c.yaw,c.pitch);teleport=c.teleports;next_tick=net_millis();
            }
            unsigned ticks=0;
            while(walk && !result && player.initialized && net_millis()>=next_tick && ticks++<4){
                McInput input={.yaw=player.yaw,.pitch=player.pitch};
                input.forward=player.ticks>20 && player.ticks<80?1:0;
                input.jump=player.ticks==30;
                mc_player_tick(&player,&world,&input);
                if(!player.waiting && mc_move(&c,player.x,player.y,player.z,player.yaw,player.pitch,player.grounded)<0)result=-1;
                next_tick+=50;
            }
            if(ticks>=4)next_tick=net_millis()+50;
        }
        if(walk)printf("walking packets=%llu ticks=%llu grounded=%d waiting=%d flight_allowed=%d\n",(unsigned long long)c.movement_packets,(unsigned long long)player.ticks,player.grounded,player.waiting,c.can_fly);
        printf("joined=%d packets=%llu chunks=%llu keepalives=%llu teleports=%llu position=%.2f,%.2f,%.2f\n", c.joined,
            (unsigned long long)c.packets, (unsigned long long)c.chunks, (unsigned long long)c.keepalives,
            (unsigned long long)c.teleports, c.x,c.y,c.z);
        if (!c.joined || !c.teleports) result = -1;
        if (view && !result && world.loaded) {
            static uint32_t pixels[MC_VIEW_W*MC_VIEW_H];
            McCamera camera = {c.x,c.y+1.62,c.z,c.yaw,20};
            mc_render_init(); uint64_t start = net_millis(); mc_render(&world,&camera,pixels);
            printf("dimension=%s min_y=%d height=%d cached=%d decoded=%llu memory=%zu render_ms=%llu\n",world.dimension,world.min_y,world.height,world.loaded,(unsigned long long)world.decoded,world.bytes,(unsigned long long)(net_millis()-start));
            FILE *image = fopen(argc > 7 ? argv[7] : "build-host/server-view.ppm","wb");
            if (image) {
                fprintf(image,"P6\n%d %d\n255\n",MC_VIEW_W,MC_VIEW_H);
                for (unsigned i = 0; i < MC_VIEW_W*MC_VIEW_H; i++) {
                    uint8_t rgb[3] = {pixels[i] & 255,(pixels[i] >> 8) & 255,(pixels[i] >> 16) & 255}; fwrite(rgb,1,3,image);
                }
                fclose(image);
            } else result = -1;
        } else if (view && !world.loaded) result = -1;
        mc_world_destroy(&world);
    } else result = -1;
    printf("%s\n", c.message); mc_close(&c); net_term(); return result < 0 ? 1 : 0;
}
