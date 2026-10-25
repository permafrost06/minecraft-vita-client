#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
static void *net_memory;
int net_init(void) {
    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (r < 0) return r;
    net_memory = calloc(1, 1024 * 1024);
    if (!net_memory) return -1;
    SceNetInitParam p = {net_memory, 1024 * 1024, 0};
    r = sceNetInit(&p);
    if (r < 0) { free(net_memory); net_memory = NULL; }
    return r;
}
void net_term(void) { sceNetTerm(); free(net_memory); sceSysmoduleUnloadModule(SCE_SYSMODULE_NET); }
uint64_t net_millis(void) { return sceKernelGetSystemTimeWide() / 1000; }
void net_close(int fd) { if (fd >= 0) sceNetSocketClose(fd); }
static int wait_socket(int fd, int write, uint64_t deadline) {
    uint64_t now = net_millis();
    if (now >= deadline) return -1;
    int ep = sceNetEpollCreate("mcwait", 0);
    if (ep < 0) return -1;
    SceNetEpollEvent ev; memset(&ev, 0, sizeof(ev));
    ev.events = write ? SCE_NET_EPOLLOUT : SCE_NET_EPOLLIN;
    int r = sceNetEpollControl(ep, SCE_NET_EPOLL_CTL_ADD, fd, &ev);
    if (r >= 0) r = sceNetEpollWait(ep, &ev, 1, (int)((deadline-now)*1000));
    sceNetEpollDestroy(ep);
    return r > 0 ? 0 : -1;
}
static int pending(void) {
    int e = *sceNetErrnoLoc();
    return e == SCE_NET_EWOULDBLOCK || e == SCE_NET_EINPROGRESS || e == SCE_NET_EINTR;
}
#define READ(fd,p,n) sceNetRecv(fd,p,n,0)
#define WRITE(fd,p,n) sceNetSend(fd,p,n,0)
int net_connect(const char *ip, uint16_t port) {
    SceNetSockaddrIn a; memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET; a.sin_port = sceNetHtons(port);
    if (sceNetInetPton(SCE_NET_AF_INET, ip, &a.sin_addr) != 1) return -1;
    int fd = sceNetSocket("minecraft", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1, error = 0; unsigned int len = sizeof(error);
    if (sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &yes, sizeof(yes)) < 0) goto fail;
    int r = sceNetConnect(fd, (SceNetSockaddr *)&a, sizeof(a));
    if (r < 0 && (!pending() || wait_socket(fd, 1, net_millis()+5000) < 0)) goto fail;
    if (sceNetGetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_ERROR, &error, &len) < 0 || error) goto fail;
    return fd;
fail: net_close(fd); return -1;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int net_init(void) { return 0; }
void net_term(void) {}
uint64_t net_millis(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
void net_close(int fd) { if (fd >= 0) close(fd); }
static int wait_socket(int fd, int write, uint64_t deadline) {
    for (;;) {
        uint64_t now = net_millis();
        if (now >= deadline) return -1;
        struct pollfd p = {fd, write ? POLLOUT : POLLIN, 0};
        int r = poll(&p, 1, (int)(deadline-now));
        if (r < 0 && errno == EINTR) continue;
        return r > 0 && !(p.revents & (POLLERR | POLLNVAL)) ? 0 : -1;
    }
}
static int pending(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
#define READ(fd,p,n) recv(fd,p,n,0)
#define WRITE(fd,p,n) send(fd,p,n,MSG_NOSIGNAL)
int net_connect(const char *ip, uint16_t port) {
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) goto fail;
    int r = connect(fd, (struct sockaddr *)&a, sizeof(a));
    if (r < 0 && (!pending() || wait_socket(fd, 1, net_millis()+5000) < 0)) goto fail;
    int error = 0; socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) goto fail;
    return fd;
fail: net_close(fd); return -1;
}
#endif
int net_ready(int fd, unsigned timeout_ms) { return wait_socket(fd, 0, net_millis()+timeout_ms) == 0; }
int net_read(int fd, void *dst, size_t n, uint64_t deadline) {
    unsigned char *p = dst;
    while (n) {
        if (wait_socket(fd, 0, deadline) < 0) return -1;
        int r = READ(fd, p, n);
        if (r < 0 && pending()) continue;
        if (r <= 0) return -1;
        p += r; n -= r;
    }
    return 0;
}
int net_write(int fd, const void *src, size_t n, uint64_t deadline) {
    const unsigned char *p = src;
    while (n) {
        if (wait_socket(fd, 1, deadline) < 0) return -1;
        int r = WRITE(fd, p, n);
        if (r < 0 && pending()) continue;
        if (r <= 0) return -1;
        p += r; n -= r;
    }
    return 0;
}
