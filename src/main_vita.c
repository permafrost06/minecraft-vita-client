#include "protocol.h"
#include "net.h"
#include "world.h"
#include "render.h"
#include <math.h>
#include "debugScreen.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>

/* Networking runs on a worker; only snapshots and commands cross the mutex. */
int _newlib_heap_size_user = 48 * 1024 * 1024;
static SceUID mutex;
static int command, running = 1;
static char server[32] = "192.168.100.1";
static unsigned port = 25565;
static McClient snapshot;
static McWorld terrain;
static uint32_t view_pixels[MC_VIEW_W*MC_VIEW_H];
static int world_packet(void *context,uint32_t id,const uint8_t *p,size_t n) {
    (void)context;
    sceKernelLockMutex(mutex,1,NULL);
    int r = mc_world_packet(&terrain,id,p,n);
    sceKernelUnlockMutex(mutex,1);
    return r;
}
static char status_text[1024] = "Press CROSS to query your server.";
static void log_stage(const char *stage, int result) {
    sceIoMkdir("ux0:data/vitablock", 0777);
    FILE *f = fopen("ux0:data/vitablock/runtime.log", "a");
    if (f) { fprintf(f, "%s: 0x%08x\n", stage, (unsigned)result); fclose(f); }
}
static void publish(McClient *c, const char *text) {
    sceKernelLockMutex(mutex, 1, NULL);
    snapshot = *c;
    if (text) snprintf(status_text, sizeof(status_text), "%.1023s", text);
    sceKernelUnlockMutex(mutex, 1);
}
static int worker(SceSize args, void *argp) {
    (void)args; (void)argp;
    McClient c; mc_init(&c);
    for (;;) {
        int action; char ip[32]; unsigned p;
        sceKernelLockMutex(mutex, 1, NULL);
        action = command; command = 0; memcpy(ip, server, sizeof(ip)); p = port;
        int active = running;
        sceKernelUnlockMutex(mutex, 1);
        if (!active) break;
        if (action == 1) {
            char *json = malloc(MC_MAX_PACKET+1); unsigned ping;
            publish(&c, "Querying server...");
            if (!json) { publish(&c, "Out of memory for status query"); continue; }
            if (mc_status(&c, ip, p, json, MC_MAX_PACKET+1, &ping) == 0) {
                FILE *file = fopen("ux0:data/vitablock/status.json", "w");
                if (file) { fputs(json, file); fclose(file); }
                publish(&c, c.message);
            } else publish(&c, c.message);
            free(json);
        } else if (action == 2) {
            publish(&c, "Connecting...");
            sceKernelLockMutex(mutex,1,NULL);
            mc_world_destroy(&terrain); mc_world_init(&terrain);
            sceKernelUnlockMutex(mutex,1);
            mc_login(&c, ip, p, "VitaPlayer");
            c.handler = world_packet;
            publish(&c, c.message);
        } else if (action == 3) {
            mc_close(&c); publish(&c, "Disconnected.");
        }
        if (c.fd >= 0) {
            int r = mc_step(&c);
            if (r) publish(&c, r < 0 && terrain.error[0] ? terrain.error : c.message);
        } else sceKernelDelayThread(20000);
    }
    mc_close(&c); return 0;
}
/* A local screenshot command avoids changing global taiHEN configuration. */
static void screenshot(void) {
    SceDisplayFrameBuf fb = {0}; fb.size = sizeof(fb);
    if (sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0 || !fb.base ||
        fb.width != 960 || fb.height != 544 || fb.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) return;
    unsigned char h[54] = {'B','M'};
    uint32_t size = 54+fb.width*fb.height*4, offset = 54, dib = 40, width = fb.width;
    int32_t height = -(int32_t)fb.height; uint16_t planes = 1, depth = 32;
    memcpy(h+2,&size,4); memcpy(h+10,&offset,4); memcpy(h+14,&dib,4);
    memcpy(h+18,&width,4); memcpy(h+22,&height,4); memcpy(h+26,&planes,2); memcpy(h+28,&depth,2);
    FILE *f = fopen("ux0:data/vitablock/shot.bmp", "wb");
    if (!f) return;
    fwrite(h,1,sizeof(h),f);
    uint32_t row[960]; const uint32_t *pixels = fb.base;
    for (unsigned y = 0; y < fb.height; y++) {
        for (unsigned x = 0; x < fb.width; x++) {
            uint32_t c = pixels[y*fb.pitch+x];
            row[x] = (c & 0xff00ff00) | ((c & 255) << 16) | ((c >> 16) & 255);
        }
        fwrite(row,4,fb.width,f);
    }
    fclose(f);
}
int main(void) {
    log_stage("main", 0);
    log_stage("display init", psvDebugScreenInit());
    PsvDebugScreenFont *large_font = psvDebugScreenScaleFont2x(psvDebugScreenGetFont());
    if (large_font) psvDebugScreenSetFont(large_font);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceIoMkdir("ux0:data/vitablock", 0777);
    /* Config: one numeric IPv4 and an optional TCP port. */
    FILE *config = fopen("ux0:data/vitablock/server.txt", "r");
    if (config) { char ip[32]; unsigned p = 25565;
        int count = fscanf(config, "%31s %u", ip, &p); fclose(config);
        if (count >= 1 && p >= 1 && p <= 65535) { memcpy(server, ip, strlen(ip)+1); port = p; }
    }
    mc_init(&snapshot); mc_world_init(&terrain); mc_render_init();
    int net_result = net_init(); log_stage("net init", net_result);
    int initialized = net_result >= 0;
    mutex = sceKernelCreateMutex("mcstate", 0, 0, NULL);
    if (!initialized || mutex < 0) { psvDebugScreenPrintf("Network or mutex initialization failed.\n"); sceKernelDelayThread(5000000); sceKernelExitProcess(1); }
    SceUID thread = sceKernelCreateThread("mcnetwork", worker, 0x10000100, 128*1024, 0, 0, NULL);
    if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0) { psvDebugScreenPrintf("Network worker failed.\n"); sceKernelDelayThread(5000000); sceKernelExitProcess(1); }
    log_stage("worker started", thread);
    uint32_t previous = 0; unsigned octet = 3; unsigned frame = 0;
    McCamera camera = {0}; uint64_t last_teleport = 0, previous_time = net_millis();
    int diagnostics = 0; float fps = 0;

    for (;;) {
        SceCtrlData pad; memset(&pad, 0, sizeof(pad)); sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~previous; previous = pad.buttons;
        int take_shot = 0;
        if (frame % 6 == 0) {
            FILE *remote = fopen("ux0:data/vitablock/command.txt", "r");
            if (remote) {
                char cmd[32] = {0}; int found = fscanf(remote, "%31s", cmd); fclose(remote);
                remove("ux0:data/vitablock/command.txt");
                if (found == 1) {
                    if (!strcmp(cmd,"shot")) take_shot = 1;
                    else if (!strcmp(cmd,"status")) pressed |= SCE_CTRL_CROSS;
                    else if (!strcmp(cmd,"login")) pressed |= SCE_CTRL_TRIANGLE;
                    else if (!strcmp(cmd,"disconnect")) pressed |= SCE_CTRL_CIRCLE;
                }
            }
        }
        if (pressed & SCE_CTRL_SELECT) break;
        if (pressed & SCE_CTRL_START) diagnostics = !diagnostics;
        sceKernelLockMutex(mutex, 1, NULL);
        if (pressed & SCE_CTRL_CROSS) command = 1;
        if (pressed & SCE_CTRL_TRIANGLE) command = 2;
        if (pressed & SCE_CTRL_CIRCLE) command = 3;
        if (pressed & SCE_CTRL_LEFT) octet = (octet+3)%4;
        if (pressed & SCE_CTRL_RIGHT) octet = (octet+1)%4;
        if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN)) {
            unsigned a[4];
            if (sscanf(server, "%u.%u.%u.%u", &a[0],&a[1],&a[2],&a[3]) == 4) {
                a[octet] = (a[octet]+((pressed & SCE_CTRL_UP) ? 1 : 255))%256;
                snprintf(server, sizeof(server), "%u.%u.%u.%u", a[0],a[1],a[2],a[3]);
            }
        }
        if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_CROSS | SCE_CTRL_TRIANGLE)) {
            FILE *out = fopen("ux0:data/vitablock/server.txt", "w");
            if (out) { fprintf(out, "%s %u\n", server, port); fclose(out); }
        }
        McClient c = snapshot; char message[1024]; memcpy(message, status_text, sizeof(message));
        char ip[32]; memcpy(ip, server, sizeof(ip));
        int loaded = terrain.loaded; size_t world_bytes = terrain.bytes;
        sceKernelUnlockMutex(mutex, 1);
        uint64_t now = net_millis(); float dt = (now-previous_time)*0.001f; previous_time = now;
        if (dt > 0) fps = fps ? fps*0.9f+0.1f/dt : 1/dt;
        if (dt > 0.2f) dt = 0.2f;
        if (!c.teleports) last_teleport = 0;
        if (c.teleports && (last_teleport != c.teleports || (pressed & SCE_CTRL_SQUARE))) {
            camera = (McCamera){c.x,c.y+1.62,c.z,c.yaw,20}; last_teleport = c.teleports;
        }
        if (c.play && c.teleports && loaded && !diagnostics) {
            float lx = ((int)pad.lx-128)/127.0f, ly = ((int)pad.ly-128)/127.0f;
            float rx = ((int)pad.rx-128)/127.0f, ry = ((int)pad.ry-128)/127.0f;
            if (fabsf(lx) < 0.16f) lx = 0;
            if (fabsf(ly) < 0.16f) ly = 0;
            if (fabsf(rx) < 0.16f) rx = 0;
            if (fabsf(ry) < 0.16f) ry = 0;
            mc_camera_look(&camera,rx*100*dt,ry*80*dt);
            mc_camera_move(&camera,-ly,lx,((pad.buttons & SCE_CTRL_RTRIGGER) ? 1 : 0)-((pad.buttons & SCE_CTRL_LTRIGGER) ? 1 : 0),dt);
            sceKernelLockMutex(mutex,1,NULL); mc_render(&terrain,&camera,view_pixels); sceKernelUnlockMutex(mutex,1);
            SceDisplayFrameBuf fb = {0}; fb.size = sizeof(fb);
            if (sceDisplayGetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME) >= 0 && fb.base) {
                uint32_t *destination = fb.base;
                for (int y = 0; y < MC_VIEW_H; y++) {
                    for (int scale_y = 0; scale_y < 4; scale_y++) {
                        uint32_t *row = destination+(y*4+scale_y)*fb.pitch;
                        for (int x = 0; x < MC_VIEW_W; x++) {
                            uint32_t color = view_pixels[y*MC_VIEW_W+x];
                            row[x*4] = row[x*4+1] = row[x*4+2] = row[x*4+3] = color;
                        }
                    }
                }
            }
            psvDebugScreenPrintf("\e[HMC %s | %d chunks | %.1f MB | %.0f FPS\n",MC_VERSION,loaded,world_bytes/(1024.0*1024),fps);
            psvDebugScreenPrintf("Sticks: fly/look  L/R: down/up  START: menu\n");
        } else if (frame % 6 == 0 || pressed) {
            psvDebugScreenPrintf("\e[H\e[2J");
            psvDebugScreenPrintf("VITA BLOCK CLIENT | Java %s | milestone 2\n\n", MC_VERSION);
            psvDebugScreenPrintf("Server: %s:%u   [editing octet %u]\n\n", ip, port, octet+1);
            psvDebugScreenPrintf("LEFT/RIGHT: select octet   UP/DOWN: change address\n");
            psvDebugScreenPrintf("CROSS: status   TRIANGLE: login as VitaPlayer\n");
            psvDebugScreenPrintf("CIRCLE: disconnect   SELECT: exit\n\n");
            psvDebugScreenPrintf("%s\n\n", message);
            psvDebugScreenPrintf("Play: %s   Joined: %s   Compression: %d\n", c.play ? "yes":"no", c.joined ? "yes":"no", c.threshold);
            psvDebugScreenPrintf("Packets: %llu   Terrain packets: %llu\n", (unsigned long long)c.packets, (unsigned long long)c.chunks);
            psvDebugScreenPrintf("Keepalives: %llu   Teleports confirmed: %llu\n", (unsigned long long)c.keepalives, (unsigned long long)c.teleports);
            psvDebugScreenPrintf("Position: %.2f, %.2f, %.2f\n\n", c.x,c.y,c.z);
            psvDebugScreenPrintf("Requires Java 1.19.4, online-mode=false for login.\n");
            psvDebugScreenPrintf("Cached: %d chunks (%.1f MB). START: terrain view.\n",loaded,world_bytes/(1024.0*1024));
            psvDebugScreenPrintf("Local inspection camera; no player movement yet.\n");
        }
        frame++;
        if (take_shot) screenshot();
        sceDisplayWaitVblankStart();
    }
    sceKernelLockMutex(mutex, 1, NULL); running = 0; sceKernelUnlockMutex(mutex, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL); sceKernelDeleteThread(thread);
    mc_world_destroy(&terrain);
    sceKernelDeleteMutex(mutex); net_term(); sceKernelExitProcess(0); return 0;
}
