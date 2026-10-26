#include "protocol.h"
#include "net.h"
#include "world.h"
#include "render.h"
#include "player.h"
#include "interaction.h"
#include <math.h>
#include "gl_render.h"
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <string.h>

/* Networking runs on a worker; snapshots, commands and terrain share a mutex. */
int _newlib_heap_size_user = 48 * 1024 * 1024;
/* The runtime shader compiler needs more than the default main-thread stack. */
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
static SceUID mutex;
static int command, running = 1;
static char server[32] = "192.168.100.1";
static unsigned port = 25565;
static McClient snapshot;
static McPlayer player_snapshot;
static McInput player_input;
static int inspection,flight_pending,jump_pending,stream_test_active;
static int slot_delta, palette_pending, break_pending, place_pending, break_held, place_held;
static McWorld terrain;
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
    if (f) { fprintf(f, "%llu ms %s: 0x%08x\n", (unsigned long long)net_millis(),stage,(unsigned)result); fclose(f); }
}
static void publish(McClient *c, const McPlayer *player, const char *text) {
    sceKernelLockMutex(mutex, 1, NULL);
    snapshot = *c; player_snapshot = *player;
    if (text) snprintf(status_text, sizeof(status_text), "%.1023s", text);
    sceKernelUnlockMutex(mutex, 1);
}
static int worker(SceSize args, void *argp) {
    (void)args; (void)argp;
    McClient c; mc_init(&c); McPlayer player={0};
    uint64_t teleport=0,next_tick=net_millis(),test_ticks=0,test_initial_chunks=0,next_use=0;
    int testing=0;double test_x=0,test_z=0;uint64_t test_deadline=0;
    for (;;) {
        int action,toggle,select,build; char ip[32]; unsigned port_number;
        sceKernelLockMutex(mutex,1,NULL);
        action=command;command=0;toggle=flight_pending;flight_pending=0;
        select=slot_delta;slot_delta=0;build=palette_pending;palette_pending=0;
        memcpy(ip,server,sizeof(ip));port_number=port;int active=running;
        sceKernelUnlockMutex(mutex,1);
        if(!active)break;
        if(action==1){
            char *json=malloc(MC_MAX_PACKET+1);unsigned ping;
            publish(&c,&player,"Querying server...");
            if(!json){publish(&c,&player,"Out of memory for status query");continue;}
            if(mc_status(&c,ip,port_number,json,MC_MAX_PACKET+1,&ping)==0){
                FILE *file=fopen("ux0:data/vitablock/status.json","w");
                if(file){fputs(json,file);fclose(file);}
            }
            free(json);publish(&c,&player,c.message);
        }else if(action==2 || action==4){
            log_stage("connect requested",action);
            publish(&c,&player,"Connecting...");
            sceKernelLockMutex(mutex,1,NULL);
            mc_world_destroy(&terrain);mc_world_init(&terrain);
            memset(&player_input,0,sizeof(player_input));inspection=0;jump_pending=0;
            break_pending=place_pending=break_held=place_held=0;
            testing=action==4;stream_test_active=testing;test_ticks=0;test_initial_chunks=0;test_deadline=net_millis()+90000;
            sceKernelUnlockMutex(mutex,1);
            log_stage("terrain reset complete",0);
            memset(&player,0,sizeof(player));teleport=0;next_tick=net_millis();
            int login_result=mc_login(&c,ip,port_number,testing?"VitaStream":"VitaPlayer");c.handler=world_packet;
            log_stage("login attempt complete",login_result);
            publish(&c,&player,c.message);
        }else if(action==3){
            testing=0;sceKernelLockMutex(mutex,1,NULL);stream_test_active=0;sceKernelUnlockMutex(mutex,1);
            log_stage("disconnect requested",0);
            mc_close(&c);publish(&c,&player,"Disconnected.");
            log_stage("disconnect complete",0);
        }
        if(testing && (c.fd<0 || net_millis()>test_deadline)){
            FILE *report=fopen("ux0:data/vitablock/stream-test.txt","w");
            if(report){fprintf(report,"passed=0 reason=connection-or-test-timeout ticks=%llu\n",(unsigned long long)test_ticks);fclose(report);}
            sceKernelLockMutex(mutex,1,NULL);testing=0;stream_test_active=0;if(!command)command=2;sceKernelUnlockMutex(mutex,1);
        }
        int changed=0;
        if(c.fd>=0){
            int was_joined=c.joined,was_ready=c.position_ready;
            int r=mc_step(&c);changed=r!=0;
            if(r<0)log_stage("network step failed",r);
            if(!was_joined && c.joined)log_stage("server joined",c.gamemode);
            if(!was_ready && c.position_ready)log_stage("spawn ready",0);
            if(!c.position_ready)player.initialized=0;
            if(c.position_ready && c.teleports!=teleport){
                mc_player_reset(&player,c.x,c.y,c.z,c.yaw,c.pitch);teleport=c.teleports;next_tick=net_millis();
                if(testing && test_ticks==0){test_x=c.x;test_z=c.z;}
                sceKernelLockMutex(mutex,1,NULL);player_input.yaw=c.yaw;player_input.pitch=c.pitch;sceKernelUnlockMutex(mutex,1);
            }
            if(!testing && toggle && c.position_ready && c.can_fly){mc_set_flying(&c,!c.flying);changed=1;}
            if(!testing && c.play && c.joined){
                if(select){mc_select_slot(&c,(c.selected_slot+select%9+9)%9);changed=1;}
                if(build && c.gamemode==1){
                    int index=-1;
                    for(int i=0;i<MC_BUILD_BLOCKS;i++)if(c.hotbar[c.selected_slot].item==mc_build_items[i])index=i;
                    mc_creative_slot(&c,c.selected_slot,mc_build_items[(index+1)%MC_BUILD_BLOCKS]);changed=1;
                }
            }
            unsigned ticks=0;
            while(c.play && c.position_ready && player.initialized && net_millis()>=next_tick && ticks++<4){
                if(testing){
                    if(c.can_fly){
                        if(!c.flying)mc_set_flying(&c,1);
                        if(test_ticks==200)test_initial_chunks=terrain.decoded;
                        double x=c.x+(test_ticks>=200?0.25:0),y=c.y+(test_ticks<200?0.25:0);
                        if(mc_move(&c,x,y,c.z,-90,0,0)<0)break;
                        player.x=c.x;player.y=c.y;player.z=c.z;player.yaw=c.yaw;player.pitch=0;
                        player.flying=1;player.grounded=0;player.waiting=0;test_ticks++;
                    }
                    next_tick+=50;changed=1;
                    if(test_ticks>=700){
                        sceKernelLockMutex(mutex,1,NULL);
                        int covered=mc_world_chunk(&terrain,(int)floor(c.x/16),(int)floor(c.z/16))!=NULL;
                        uint64_t received=terrain.decoded,incoming=received-test_initial_chunks;
                        int passed=c.teleports==1 && c.x-test_x>=64 && incoming>0 && covered;
                        FILE *report=fopen("ux0:data/vitablock/stream-test.txt","w");
                        if(report){
                            fprintf(report,"passed=%d distance=%.2f,%.2f received=%llu new=%llu cached=%d center=%d,%d unloads=%llu evictions=%llu covered=%d teleports=%llu keepalives=%llu\n",passed,c.x-test_x,c.z-test_z,(unsigned long long)received,(unsigned long long)incoming,terrain.loaded,terrain.center_x,terrain.center_z,(unsigned long long)terrain.unloads,(unsigned long long)terrain.evictions,covered,(unsigned long long)c.teleports,(unsigned long long)c.keepalives);
                            fclose(report);
                        }
                        testing=0;stream_test_active=0;command=2;
                        sceKernelUnlockMutex(mutex,1);
                        break;
                    }
                    continue;
                }
                sceKernelLockMutex(mutex,1,NULL);
                McInput input=player_input;
                if(jump_pending){input.jump=1;jump_pending=0;}
                player.flying=c.flying;
                mc_player_tick(&player,&terrain,&input);
                sceKernelUnlockMutex(mutex,1);
                if(!player.waiting)mc_move(&c,player.x,player.y,player.z,player.yaw,player.pitch,player.grounded);
                McWorld *world=&terrain; int hit=0,place=0;
                sceKernelLockMutex(mutex,1,NULL);
                if(!inspection && !player.waiting && net_millis()>=next_use){
                    hit=break_pending || break_held;place=!hit && (place_pending || place_held);
                    break_pending=place_pending=0;
                    /* Resolve under the same lock as chunk updates. Sending is
                       done outside it so the UI never waits on network I/O. */
                }
                McTarget target;int targeted=(hit || place) && mc_target(world,c.x,c.y+MC_PLAYER_EYE,c.z,c.yaw,c.pitch,&target);
                sceKernelUnlockMutex(mutex,1);
                if(hit || place){
                    if(targeted && c.gamemode==1 && (!place || c.hotbar[c.selected_slot].count))
                        mc_block_action(&c,place,target.x,target.y,target.z,target.face,target.cursor);
                    next_use=net_millis()+200;
                }
                next_tick+=50;changed=1;
            }
            if(ticks>=4)next_tick=net_millis()+50;
            if(changed)publish(&c,&player,r<0 && terrain.error[0]?terrain.error:c.message);
        }else sceKernelDelayThread(20000);
    }
    mc_close(&c);return 0;
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
static void hud_panel(float x, float y, float w, float h) { mc_gl_rect(x, y, w, h, 0x90000000); }
static void draw_menu(const McClient *c, const char *ip, unsigned octet, const char *message, int loaded, size_t world_bytes, int over_world) {
    if (!over_world) {
        mc_gl_rect(0, 0, 960, 272, 0xff1c2a3a); mc_gl_rect(0, 272, 960, 272, 0xff121a24);
    }
    float x = 120, y = 52, w = 720;
    mc_gl_rect(x, y, w, 440, 0xd0101820); mc_gl_rect(x, y, w, 4, 0xff6cc04a);
    mc_gl_text(x+28, y+24, 3, 0xffffffff, "VITA BLOCK CLIENT");
    mc_gl_textf(x+30, y+60, 1.5f, 0xff9fb4c8, "Minecraft Java %s  -  hardware renderer", MC_VERSION);
    /* Address with the selected octet highlighted. */
    unsigned a[4] = {0}; sscanf(ip, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]);
    float ax = x+30, ay = y+100;
    mc_gl_text(ax, ay, 2, 0xffc8d4e0, "Server"); ax += 120;
    for (unsigned i = 0; i < 4; i++) {
        char part[8]; snprintf(part, sizeof(part), "%u", a[i]);
        float pw = mc_gl_text_width(part, 2);
        if (i == octet) mc_gl_rect(ax-4, ay-4, pw+8, 24, 0xff3a6a2a);
        mc_gl_text(ax, ay, 2, 0xffffffff, part); ax += pw;
        if (i < 3) { mc_gl_text(ax, ay, 2, 0xff8090a0, "."); ax += 16; }
    }
    mc_gl_textf(ax, ay, 2, 0xff8090a0, ":%u", port);
    mc_gl_text(x+30, y+140, 1.5f, 0xffa0b0c0, "LEFT/RIGHT select octet   UP/DOWN change");
    mc_gl_text(x+30, y+162, 1.5f, 0xffa0b0c0, "CROSS status   TRIANGLE login as VitaPlayer");
    mc_gl_text(x+30, y+184, 1.5f, 0xffa0b0c0, "CIRCLE disconnect   SELECT exit   START close menu");
    char wrapped[1024]; snprintf(wrapped, sizeof(wrapped), "%s", message);
    /* Wrap the status message at 58 columns. */
    float my = y+222; char *line = wrapped;
    for (int rows = 0; *line && rows < 5; rows++) {
        char buffer[64]; size_t n = strlen(line); if (n > 58) n = 58;
        char *newline = memchr(line, '\n', n); if (newline) n = newline - line;
        memcpy(buffer, line, n); buffer[n] = 0;
        mc_gl_text(x+30, my, 1.5f, 0xfff0e0a0, buffer); my += 18;
        line += n; if (*line == '\n') line++;
    }
    mc_gl_textf(x+30, y+330, 1.5f, 0xffc8d4e0, "Play: %s   Joined: %s   Compression: %d", c->play ? "yes" : "no", c->joined ? "yes" : "no", c->threshold);
    mc_gl_textf(x+30, y+350, 1.5f, 0xffc8d4e0, "Packets %llu   Terrain %llu   Keepalives %llu", (unsigned long long)c->packets, (unsigned long long)c->chunks, (unsigned long long)c->keepalives);
    mc_gl_textf(x+30, y+370, 1.5f, 0xffc8d4e0, "Cached %d chunks (%.1f MB)   Moves sent %llu", loaded, world_bytes/(1024.0*1024), (unsigned long long)c->movement_packets);
    mc_gl_text(x+30, y+400, 1.5f, 0xff8090a0, "Requires a Java 1.19.4 server with online-mode=false.");
}
int main(void) {
    log_stage("main: hardware renderer", 0);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceIoMkdir("ux0:data/vitablock", 0777);
    /* Config: one numeric IPv4 and an optional TCP port. */
    FILE *config = fopen("ux0:data/vitablock/server.txt", "r");
    if (config) { char ip[32]; unsigned p = 25565;
        int count = fscanf(config, "%31s %u", ip, &p); fclose(config);
        if (count >= 1 && p >= 1 && p <= 65535) { memcpy(server, ip, strlen(ip)+1); port = p; }
    }
    int gl_result = mc_gl_init(); log_stage("gl init", gl_result);
    if (gl_result < 0) { sceKernelDelayThread(1000000); sceKernelExitProcess(1); }
    mc_init(&snapshot); mc_world_init(&terrain);
    int net_result = net_init(); log_stage("net init", net_result);
    int initialized = net_result >= 0;
    mutex = sceKernelCreateMutex("mcstate", 0, 0, NULL);
    if (!initialized || mutex < 0) {
        log_stage("fatal initialization",initialized?mutex:net_result);
        for (int i = 0; i < 300; i++) { mc_gl_begin(); mc_gl_text(40, 40, 2, 0xffff8080, "Network or mutex initialization failed."); mc_gl_end(); }
        sceKernelExitProcess(1);
    }
    SceUID thread = sceKernelCreateThread("mcnetwork", worker, 0x10000100, 128*1024, 0, 0, NULL);
    if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0) { log_stage("fatal worker start",thread); sceKernelExitProcess(1); }
    log_stage("worker started", thread);
    log_stage("mesher started", mc_gl_start_worker(&terrain, mutex));
    uint32_t previous = 0; unsigned octet = 3; unsigned frame = 0;
    McCamera camera = {0}; uint64_t last_teleport = 0, previous_time = net_millis();
    int diagnostics = 0, debug_info = 0; float fps = 0, frame_ms = 0;
    uint64_t next_health=0;
    double smooth[2][3] = {{0}}; uint64_t smooth_ticks = 0, smooth_time = 0;

    for (;;) {
        uint64_t frame_start = sceKernelGetProcessTimeWide();
        SceCtrlData pad; memset(&pad, 0, sizeof(pad));
        pad.lx = pad.ly = pad.rx = pad.ry = 128;
        if (sceCtrlPeekBufferPositive(0,&pad,1) <= 0) {
            pad.buttons = 0; pad.lx = pad.ly = pad.rx = pad.ry = 128;
        }
        uint32_t pressed = pad.buttons & ~previous; previous = pad.buttons;
        int take_shot=0,force_login=0,force_status=0,reset_view=0,test_stream=0,force_debug=0;
        if (frame % 6 == 0) {
            FILE *remote = fopen("ux0:data/vitablock/command.txt", "r");
            if (remote) {
                char cmd[32] = {0}; int found = fscanf(remote, "%31s", cmd); fclose(remote);
                remove("ux0:data/vitablock/command.txt");
                if (found == 1) {
                    if (!strcmp(cmd,"shot")) take_shot = 1;
                    else if (!strcmp(cmd,"status")) force_status=1;
                    else if (!strcmp(cmd,"login")) force_login=1;
                    else if (!strcmp(cmd,"disconnect")) pressed |= SCE_CTRL_CIRCLE;
                    else if (!strcmp(cmd,"menu")) pressed |= SCE_CTRL_START;
                    else if (!strcmp(cmd,"reset")) reset_view=1;
                    else if (!strcmp(cmd,"jump")) pressed |= SCE_CTRL_CROSS;
                    else if (!strcmp(cmd,"fly")) pressed |= SCE_CTRL_TRIANGLE;
                    else if (!strcmp(cmd,"inspect")) pressed |= SCE_CTRL_SQUARE;
                    else if (!strcmp(cmd,"debug")) force_debug=1;
                    else if (!strcmp(cmd,"break")) pressed |= SCE_CTRL_LTRIGGER;
                    else if (!strcmp(cmd,"place")) pressed |= SCE_CTRL_RTRIGGER;
                    else if (!strcmp(cmd,"slotnext")) pressed |= SCE_CTRL_RIGHT;
                    else if (!strcmp(cmd,"blocknext")) pressed |= SCE_CTRL_DOWN;
                    else if (!strcmp(cmd,"streamtest")) test_stream=1;
                    else if (!strncmp(cmd,"turn",4)) { float yaw = 0; if (sscanf(cmd+4,"%f",&yaw)==1) camera.yaw = fmodf(camera.yaw+yaw,360.0f); }
                    else if (!strncmp(cmd,"pitch",5)) { float pitch = 0; if (sscanf(cmd+5,"%f",&pitch)==1) mc_camera_look(&camera,0,pitch); }
                }
            }
        }
        int exit_requested=0;
        if (pressed & SCE_CTRL_START) diagnostics = !diagnostics;
        sceKernelLockMutex(mutex, 1, NULL);
        int menu=!snapshot.play || diagnostics;
        if(menu && (pressed&SCE_CTRL_SELECT))exit_requested=1;
        if(force_status || (menu && (pressed&SCE_CTRL_CROSS)))command=1;
        if(force_login || (menu && (pressed&SCE_CTRL_TRIANGLE)))command=2;
        if(test_stream)command=4;
        if(!menu && !inspection && (pressed&SCE_CTRL_TRIANGLE))flight_pending=1;
        if(!menu && !inspection && (pressed&SCE_CTRL_CROSS))jump_pending=1;
        int can_use=!menu && !inspection && !stream_test_active;
        break_held=can_use && !!(pad.buttons&SCE_CTRL_LTRIGGER);
        place_held=can_use && !!(pad.buttons&SCE_CTRL_RTRIGGER);
        if(can_use){
            if(pressed&SCE_CTRL_LTRIGGER)break_pending=1;
            if(pressed&SCE_CTRL_RTRIGGER)place_pending=1;
            if(pressed&SCE_CTRL_LEFT)slot_delta--;
            if(pressed&SCE_CTRL_RIGHT)slot_delta++;
            if(pressed&SCE_CTRL_DOWN)palette_pending=1;
        }else break_pending=place_pending=0;
        if(!menu && (pressed&SCE_CTRL_SQUARE)){inspection=!inspection;reset_view=1;}
        if (pressed & SCE_CTRL_CIRCLE) command = 3;
        if (menu && (pressed & SCE_CTRL_LEFT)) octet = (octet+3)%4;
        if (menu && (pressed & SCE_CTRL_RIGHT)) octet = (octet+1)%4;
        if (menu && (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN))) {
            unsigned a[4];
            if (sscanf(server, "%u.%u.%u.%u", &a[0],&a[1],&a[2],&a[3]) == 4) {
                a[octet] = (a[octet]+((pressed & SCE_CTRL_UP) ? 1 : 255))%256;
                snprintf(server, sizeof(server), "%u.%u.%u.%u", a[0],a[1],a[2],a[3]);
            }
        }
        if (menu && (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_CROSS | SCE_CTRL_TRIANGLE))) {
            FILE *out = fopen("ux0:data/vitablock/server.txt", "w");
            if (out) { fprintf(out, "%s %u\n", server, port); fclose(out); }
        }
        McClient c = snapshot; McPlayer player=player_snapshot; int inspect=inspection,testing_view=stream_test_active; char message[1024]; memcpy(message, status_text, sizeof(message));
        char ip[32]; memcpy(ip, server, sizeof(ip));
        int loaded = terrain.loaded; size_t world_bytes = terrain.bytes;
        uint64_t received=terrain.decoded,unloads=terrain.unloads,centers=terrain.center_updates;
        int center_x=terrain.center_x,center_z=terrain.center_z,view_distance=terrain.view_distance;
        sceKernelUnlockMutex(mutex, 1);
        if(exit_requested){log_stage("exit requested by Select in menu",pad.buttons);break;}
        uint64_t now = net_millis(); float dt = (now-previous_time)*0.001f; previous_time = now;
        if (dt > 0) fps = fps ? fps*0.9f+0.1f/dt : 1/dt;
        if (dt > 0.2f) dt = 0.2f;
        if(!c.position_ready)last_teleport=0;
        if(player.initialized && (last_teleport!=c.teleports || reset_view)){
            camera=(McCamera){player.x,player.y+MC_PLAYER_EYE,player.z,player.yaw,player.pitch};last_teleport=c.teleports;
        }
        if(diagnostics || inspect || !c.play || !player.initialized){
            sceKernelLockMutex(mutex,1,NULL);
            player_input=(McInput){.yaw=player.yaw,.pitch=player.pitch};
            sceKernelUnlockMutex(mutex,1);
        }
        int world_view = c.play && player.initialized && loaded;
        mc_gl_begin();
        if (world_view) {
            if (!diagnostics) {
                if ((pressed & SCE_CTRL_SELECT) || force_debug) debug_info = !debug_info;
                float lx = mc_camera_axis(pad.lx), ly = mc_camera_axis(pad.ly);
                float rx = mc_camera_axis(pad.rx), ry = mc_camera_axis(pad.ry);
                mc_camera_look(&camera,rx*120*dt,ry*90*dt);
                float vertical=inspect?((pad.buttons&SCE_CTRL_RTRIGGER)?1:0)-((pad.buttons&SCE_CTRL_LTRIGGER)?1:0):
                    ((pad.buttons&SCE_CTRL_UP)?1:0)-((pad.buttons&SCE_CTRL_CROSS)?1:0);
                if(inspect)mc_camera_move(&camera,-ly,lx,vertical,dt);
                else{
                    sceKernelLockMutex(mutex,1,NULL);
                    player_input=(McInput){.forward=-ly,.right=lx,.vertical=vertical,.yaw=camera.yaw,.pitch=camera.pitch,.jump=!!(pad.buttons&SCE_CTRL_CROSS)};
                    sceKernelUnlockMutex(mutex,1);
                }
            }
            if (!inspect) {
                /* Physics runs at 20 Hz; interpolate the eye between the last two ticks. */
                uint64_t us = sceKernelGetProcessTimeWide();
                if (player.ticks != smooth_ticks) {
                    double jump = fabs(player.x-smooth[1][0])+fabs(player.y-smooth[1][1])+fabs(player.z-smooth[1][2]);
                    memcpy(smooth[0], smooth[1], sizeof(smooth[0]));
                    smooth[1][0] = player.x; smooth[1][1] = player.y; smooth[1][2] = player.z;
                    if (jump > 4 || !smooth_ticks) memcpy(smooth[0], smooth[1], sizeof(smooth[0]));
                    smooth_ticks = player.ticks; smooth_time = us;
                }
                double t = (us-smooth_time)/50000.0; if (t > 1) t = 1;
                camera.x = smooth[0][0]+(smooth[1][0]-smooth[0][0])*t;
                camera.y = smooth[0][1]+(smooth[1][1]-smooth[0][1])*t+MC_PLAYER_EYE;
                camera.z = smooth[0][2]+(smooth[1][2]-smooth[0][2])*t;
            }
            sceKernelLockMutex(mutex,1,NULL);
            mc_gl_update(&terrain,&camera,0);
            int distance = terrain.view_distance;
            McTarget aimed;int has_target=!inspect && mc_target(&terrain,camera.x,camera.y,camera.z,camera.yaw,camera.pitch,&aimed);
            sceKernelUnlockMutex(mutex,1);
            mc_gl_draw_world(&camera,distance);
            const McGlStats *gs = mc_gl_stats();
            /* Crosshair */
            uint32_t cross_color=has_target?0xffa8ed80:0xc0ffffff;
            mc_gl_rect(479,264,2,16,cross_color);mc_gl_rect(472,271,16,2,cross_color);
            if(has_target)mc_gl_textf(380,300,1,0xd0ffffff,"Block %d %d %d (%.1fm)",aimed.x,aimed.y,aimed.z,aimed.distance);
            const char *mode = testing_view?"STREAM TEST":inspect?"INSPECT (local camera)":player.flying?"FLYING":player.waiting?"WAITING FOR TERRAIN":"WALKING";
            hud_panel(8, 8, debug_info ? 470 : 380, debug_info ? 110 : 58);
            mc_gl_textf(16, 16, 1.5f, 0xffffffff, "MC %s   %.0f FPS", MC_VERSION, fps);
            mc_gl_textf(16, 36, 1.5f, 0xffd8e4f0, "%.1f %.1f %.1f  %s", camera.x, camera.y-MC_PLAYER_EYE, camera.z, mode);
            if (debug_info) {
                mc_gl_textf(16, 56, 1.5f, 0xffb8c8d8, "chunks %d/%llu  sections %u vis %u", loaded, (unsigned long long)received, gs->sections, gs->visible);
                mc_gl_textf(16, 76, 1.5f, 0xffb8c8d8, "quads %u  queue %u  build %.1fms/section", gs->drawn_quads, gs->pending, gs->build_ms);
                mc_gl_textf(16, 96, 1.5f, 0xffb8c8d8, "cpu %.1fms  yaw %.0f pitch %.0f", frame_ms, camera.yaw, camera.pitch);
            }
            if(!inspect){
                for(int i=0;i<9;i++){
                    float hx=246+i*52;
                    mc_gl_rect(hx,466,48,44,i==c.selected_slot?0xe0b5a078:0xc0202830);
                    mc_gl_textf(hx+6,472,1.5f,0xffffffff,"%d",i+1);
                    if(c.hotbar[i].count)mc_gl_textf(hx+22,472,1,0xffffffff,"%d",c.hotbar[i].count);
                    mc_gl_textf(hx+3,492,1,0xffffffff,"%.5s",mc_item_label(c.hotbar[i].item));
                }
                mc_gl_textf(350,446,1.5f,0xffffffff,"%s (#%d)%s",mc_item_label(c.hotbar[c.selected_slot].item),c.hotbar[c.selected_slot].item,c.gamemode==1?"":" (creative required)");
                mc_gl_text(16,520,1.25f,0xd0ffffff,"L break  R place  LEFT/RIGHT slot  DOWN block  X jump/down  UP fly up  START menu");
            }
            if (diagnostics) draw_menu(&c, ip, octet, message, loaded, world_bytes, 1);
        } else {
            draw_menu(&c, ip, octet, message, loaded, world_bytes, 0);
        }
        frame++;
        if (take_shot) {
            FILE *stats = fopen("ux0:data/vitablock/capture.txt","w");
            if (stats) {
                const McGlStats *gs = mc_gl_stats();
                fprintf(stats,"buttons=%08x sticks=%u,%u,%u,%u\ncamera=%.3f,%.3f,%.3f yaw=%.2f pitch=%.2f\nserver=%.3f,%.3f,%.3f teleports=%llu keepalives=%llu packets=%llu chunks=%d fps=%.1f cpu_ms=%.2f\n",pad.buttons,pad.lx,pad.ly,pad.rx,pad.ry,camera.x,camera.y,camera.z,camera.yaw,camera.pitch,c.x,c.y,c.z,(unsigned long long)c.teleports,(unsigned long long)c.keepalives,(unsigned long long)c.packets,loaded,fps,frame_ms);
                fprintf(stats,"player=%.3f,%.3f,%.3f grounded=%d flying=%d waiting=%d moves=%llu inspect=%d connected=%d\n",player.x,player.y,player.z,player.grounded,player.flying,player.waiting,(unsigned long long)c.movement_packets,inspect,c.play);
                fprintf(stats,"message=%s\n",message);
                fprintf(stats,"interaction breaks=%llu places=%llu sequence=%u ack=%u selected=%d item=%d inventory=%llu\n",(unsigned long long)c.break_packets,(unsigned long long)c.place_packets,c.interaction_sequence,c.interaction_ack,c.selected_slot,c.hotbar[c.selected_slot].item,(unsigned long long)c.inventory_updates);
                fprintf(stats,"stream received=%llu unloaded=%llu centers=%llu center=%d,%d distance=%d memory=%u\n",(unsigned long long)received,(unsigned long long)unloads,(unsigned long long)centers,center_x,center_z,view_distance,(unsigned)world_bytes);
                fprintf(stats,"render sections=%u visible=%u quads=%u meshed=%u mesh_ms=%.2f build_ms=%.2f pending=%u dropped=%u\n",gs->sections,gs->visible,gs->drawn_quads,gs->meshed,gs->mesh_ms,gs->build_ms,gs->pending,gs->dropped);
                fclose(stats);
            }
        }
        frame_ms = frame_ms*0.9f + 0.1f*(sceKernelGetProcessTimeWide()-frame_start)/1000.0f;
        mc_gl_end();
        if(net_millis()>=next_health){
            SceKernelFreeMemorySizeInfo memory={.size=sizeof(memory)};
            int memory_result=sceKernelGetFreeMemorySize(&memory);
            const McGlStats *gs=mc_gl_stats();
            FILE *health=fopen("ux0:data/vitablock/health.txt","w");
            if(health){
                fprintf(health,"time_ms=%llu frame=%u connected=%d joined=%d position_ready=%d menu=%d\n",(unsigned long long)net_millis(),frame,c.play,c.joined,c.position_ready,diagnostics);
                fprintf(health,"position=%.3f,%.3f,%.3f chunks=%d received=%llu moves=%llu teleports=%llu\n",c.x,c.y,c.z,loaded,(unsigned long long)received,(unsigned long long)c.movement_packets,(unsigned long long)c.teleports);
                fprintf(health,"memory_result=%d user_free=%d cdram_free=%d phycont_free=%d\n",memory_result,memory.size_user,memory.size_cdram,memory.size_phycont);
                fprintf(health,"vgl_free_vram=%u vgl_free_ram=%u vgl_free_phycont=%u gl_error=0x%x\n",(unsigned)vglMemFree(VGL_MEM_VRAM),(unsigned)vglMemFree(VGL_MEM_RAM),(unsigned)vglMemFree(VGL_MEM_PHYCONT),(unsigned)glGetError());
                fprintf(health,"battery_percent=%d charging=%d low_battery=%d battery_voltage_raw=%d battery_temp_raw=%d\n",scePowerGetBatteryLifePercent(),scePowerIsBatteryCharging(),scePowerIsLowBattery(),scePowerGetBatteryVolt(),scePowerGetBatteryTemp());
                fprintf(health,"sections=%u visible=%u quads=%u pending=%u fps=%.1f\n",gs->sections,gs->visible,gs->drawn_quads,gs->pending,fps);
                fclose(health);
            }
            next_health=net_millis()+5000;
        }
        if (take_shot) {
            log_stage("framebuffer capture begin",0);
            glFinish();sceDisplayWaitVblankStart();screenshot();
            log_stage("framebuffer capture complete",0);
        }
    }
    sceKernelLockMutex(mutex, 1, NULL); running = 0; sceKernelUnlockMutex(mutex, 1);
    sceKernelWaitThreadEnd(thread, NULL, NULL); sceKernelDeleteThread(thread);
    /* The mesher can still gather terrain under mutex until it has joined. */
    mc_gl_term();
    mc_world_destroy(&terrain);
    sceKernelDeleteMutex(mutex);net_term();log_stage("clean process exit",0);sceKernelExitProcess(0);return 0;
}
