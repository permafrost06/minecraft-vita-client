#include "nbt.h"
#include <string.h>
int mc_nbt_payload(McReader *r, unsigned type, unsigned depth, McNbt *node) {
    if (r->error || depth > 32 || type > 12) { r->error = 1; return -1; }
    size_t start = r->pos;
    if (type >= 1 && type <= 6) {
        static const uint8_t sizes[] = {0,1,2,4,8,4,8}; mc_skip(r,sizes[type]);
    } else if (type == 7 || type == 11 || type == 12) {
        uint32_t count = mc_u32(r); size_t stride = type == 7 ? 1 : type == 11 ? 4 : 8;
        if (count > (r->n-r->pos)/stride) r->error = 1;
        else mc_skip(r,(size_t)count*stride);
    } else if (type == 8) { uint16_t n = mc_u16(r); mc_skip(r,n); }
    else if (type == 9) {
        unsigned child = mc_u8(r); uint32_t count = mc_u32(r);
        if (child > 12 || count > 1048576 || (child == 0 && count)) r->error = 1;
        else for (uint32_t i = 0; i < count && !r->error; i++) mc_nbt_payload(r,child,depth+1,NULL);
    } else if (type == 10) {
        for (;;) {
            unsigned child = mc_u8(r);
            if (r->error || !child) break;
            uint16_t name = mc_u16(r); mc_skip(r,name);
            if (mc_nbt_payload(r,child,depth+1,NULL) < 0) break;
        }
    } else if (type == 0) { r->error = 1; }
    if (r->error) return -1;
    if (node) *node = (McNbt){type,r->p+start,r->pos-start};
    return 0;
}
int mc_nbt_root(McReader *r, McNbt *node) {
    unsigned type = mc_u8(r);
    if (type != 10) { r->error = 1; return -1; }
    uint16_t name = mc_u16(r); mc_skip(r,name);
    return mc_nbt_payload(r,type,0,node);
}
int mc_nbt_find(McNbt compound, const char *key, McNbt *node) {
    if (compound.type != 10) return -1;
    McReader r = {compound.data,compound.size,0,0}; size_t match = strlen(key);
    while (!r.error) {
        unsigned type = mc_u8(&r); if (!type) return -1;
        uint16_t n = mc_u16(&r); size_t pos = r.pos;
        if (mc_skip(&r,n) < 0) return -1;
        int equal = n == match && !memcmp(r.p+pos,key,n);
        McNbt child;
        if (mc_nbt_payload(&r,type,0,&child) < 0) return -1;
        if (equal) { *node = child; return 0; }
    }
    return -1;
}
int mc_nbt_string(McNbt node, char *out, size_t capacity) {
    if (node.type != 8) return -1;
    McReader r = {node.data,node.size,0,0}; uint16_t n = mc_u16(&r);
    if (r.error || n >= capacity || n != r.n-r.pos) return -1;
    memcpy(out,r.p+r.pos,n); out[n] = 0; return 0;
}
int mc_nbt_int(McNbt node, int *value) {
    if (node.type != 3 || node.size != 4) return -1;
    McReader r = {node.data,node.size,0,0}; *value = (int32_t)mc_u32(&r); return 0;
}
