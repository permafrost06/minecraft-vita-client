#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
int main(void) {
    uint32_t values[] = {0,1,127,128,255,762,2097151,2147483647,2147483648u,4294967295u};
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        uint8_t p[5]; size_t n = mc_varint_write(p, values[i]);
        McReader r = {p,n,0,0}; CHECK(mc_varint_read(&r) == values[i]); CHECK(!r.error && r.pos == n);
        for (size_t j = 0; j < n; j++) { r = (McReader){p,j,0,0}; mc_varint_read(&r); CHECK(r.error); }
    }
    uint8_t bad[][6] = {{128,128,128,128,128,0},{255,255,255,255,31,0}};
    for (int i = 0; i < 2; i++) { McReader r = {bad[i],6,0,0}; mc_varint_read(&r); CHECK(r.error); }
    McClient c; mc_init(&c); CHECK(c.fd == -1 && c.threshold == -1);
    CHECK(mc_login(&c, "127.0.0.1", 1, "bad name") < 0); CHECK(c.fd == -1);
    puts("Protocol boundary checks passed"); return 0;
}
