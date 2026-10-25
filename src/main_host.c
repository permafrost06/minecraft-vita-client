#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "Usage: %s status|login IPv4 [port] [username] [seconds]\n", argv[0]); return 2; }
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
    } else if (!strcmp(argv[1], "login")) {
        result = mc_login(&c, argv[2], (uint16_t)port, argc > 4 ? argv[4] : "VitaPlayer");
        uint64_t end_time = net_millis()+(uint64_t)(argc > 5 ? atoi(argv[5]) : 40)*1000;
        while (!result && net_millis() < end_time) {
            if (mc_step(&c) < 0) result = -1;
        }
        printf("joined=%d packets=%llu chunks=%llu keepalives=%llu teleports=%llu position=%.2f,%.2f,%.2f\n", c.joined,
            (unsigned long long)c.packets, (unsigned long long)c.chunks, (unsigned long long)c.keepalives,
            (unsigned long long)c.teleports, c.x,c.y,c.z);
        if (!c.joined || !c.teleports) result = -1;
    } else result = -1;
    printf("%s\n", c.message); mc_close(&c); net_term(); return result < 0 ? 1 : 0;
}
