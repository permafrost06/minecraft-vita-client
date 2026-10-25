#ifndef MC_NET_H
#define MC_NET_H
#include <stddef.h>
#include <stdint.h>
int net_init(void);
void net_term(void);
uint64_t net_millis(void);
int net_ready(int fd, unsigned timeout_ms);
int net_connect(const char *ip, uint16_t port);
void net_close(int fd);
int net_read(int fd, void *dst, size_t n, uint64_t deadline);
int net_write(int fd, const void *src, size_t n, uint64_t deadline);
#endif
