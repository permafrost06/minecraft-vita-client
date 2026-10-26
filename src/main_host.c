#include "protocol.h"
#include "net.h"
#include "world.h"
#include "render.h"
#include "player.h"
#include "entities.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static McEntities entities;
static int world_packet(void *context,uint32_t id,const uint8_t *p,size_t n) {
    McWorld *w = context; int r = mc_world_packet(w,id,p,n);
    if(!r && mc_entities_packet(&entities,id,p,n,net_millis())<0){fprintf(stderr,"Entities: %s\n",entities.error);r=-1;}
    if (r < 0) {
        fprintf(stderr,"World: %s\n",w->error);
        if(id==0x24){FILE *f=fopen(".deps/failing-chunk.bin","wb");if(f){fwrite(p,1,n,f);fclose(f);}}
    }
    if(id==0x28){FILE *f=fopen(".deps/latest-join.bin","wb");if(f){fwrite(p,1,n,f);fclose(f);}}
    return r;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "Usage: %s status|login|view|walk|stream IPv4 [port] [username] [seconds]\n", argv[0]); return 2; }
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
    } else if (!strcmp(argv[1], "login") || !strcmp(argv[1], "view") || !strcmp(argv[1],"walk") || !strcmp(argv[1],"stream")) {
        McWorld world; mc_world_init(&world); int stream=!strcmp(argv[1],"stream"), walk = !strcmp(argv[1],"walk"), view = stream || walk || !strcmp(argv[1],"view");
        McPlayer player = {0};uint64_t teleport=0,next_tick=0,next_log=0,stream_ticks=0;
        double start_x=0,start_z=0;uint64_t start_chunks=0;
        result = mc_login(&c, argv[2], (uint16_t)port, argc > 4 ? argv[4] : "VitaPlayer");
        if (view) { c.handler = world_packet; c.handler_context = &world; }
        uint64_t end_time = net_millis()+(uint64_t)(argc > 5 ? atoi(argv[5]) : 40)*1000;
        while (!result && net_millis() < end_time) {
            if (mc_step(&c) < 0) result = -1;
            if ((walk || stream) && c.position_ready && c.teleports != teleport) {
                mc_player_reset(&player,c.x,c.y,c.z,c.yaw,c.pitch);teleport=c.teleports;next_tick=net_millis();
                if(stream){start_x=c.x;start_z=c.z;start_chunks=world.decoded;}
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
            if(stream && c.position_ready && c.can_fly){
                if(!c.flying && mc_set_flying(&c,1)<0)result=-1;
                while(!result && net_millis()>=next_tick && ticks++<4){
                    /* Ascend above local terrain, then cross multiple chunk columns. */
                    if(stream_ticks==200)start_chunks=world.decoded;
                    double x=c.x+(stream_ticks>=200?0.25:0),y=c.y+(stream_ticks<200?0.25:0);
                    if(mc_move(&c,x,y,c.z,-90,0,0)<0)result=-1;
                    stream_ticks++;next_tick+=50;
                }
                if(net_millis()>=next_log){
                    printf("stream x=%.2f y=%.2f center=%d,%d centers=%llu received=%llu cached=%d unloads=%llu evictions=%llu\n",c.x,c.y,world.center_x,world.center_z,(unsigned long long)world.center_updates,(unsigned long long)world.decoded,world.loaded,(unsigned long long)world.unloads,(unsigned long long)world.evictions);fflush(stdout);next_log=net_millis()+5000;
                }
            }
            if(ticks>=4)next_tick=net_millis()+50;
        }
        printf("entities cached=%d spawned=%llu moved=%llu metadata=%llu equipment=%llu removed=%llu overflow=%llu\n",entities.count,(unsigned long long)entities.spawned,(unsigned long long)entities.moved,(unsigned long long)entities.metadata,(unsigned long long)entities.equipment,(unsigned long long)entities.removed,(unsigned long long)entities.overflow);
        if(walk)printf("walking packets=%llu ticks=%llu grounded=%d waiting=%d flight_allowed=%d\n",(unsigned long long)c.movement_packets,(unsigned long long)player.ticks,player.grounded,player.waiting,c.can_fly);
        printf("joined=%d packets=%llu chunks=%llu keepalives=%llu teleports=%llu position=%.2f,%.2f,%.2f\n", c.joined,
            (unsigned long long)c.packets, (unsigned long long)c.chunks, (unsigned long long)c.keepalives,
            (unsigned long long)c.teleports, c.x,c.y,c.z);
        printf("compat login_plugins=%llu packs_declined=%llu packs_forced=%llu\n",(unsigned long long)c.login_plugin_requests,
            (unsigned long long)c.resource_packs_declined,(unsigned long long)c.resource_packs_forced);
        if (!c.joined || !c.teleports) result = -1;
        if(stream){
            printf("stream distance=%.2f,%.2f incoming=%llu\n",c.x-start_x,c.z-start_z,(unsigned long long)(world.decoded-start_chunks));
            if(c.teleports!=1 || c.x-start_x<64 || world.decoded<=start_chunks || !mc_world_chunk(&world,(int)floor(c.x/16),(int)floor(c.z/16)))result=-1;
        }
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
