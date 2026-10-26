#include "reader.h"
#include <string.h>
#include <limits.h>
#include "nbt.h"
int mc_skip(McReader *r, size_t count) {
    if (r->error || count > r->n-r->pos) { r->error = 1; return -1; }
    r->pos += count; return 0;
}
uint8_t mc_u8(McReader *r) { if (r->pos >= r->n) { r->error = 1; return 0; } return r->p[r->pos++]; }
uint16_t mc_u16(McReader *r) { uint16_t a = mc_u8(r); return (a << 8) | mc_u8(r); }
uint32_t mc_u32(McReader *r) { uint32_t a = mc_u16(r); return (a << 16) | mc_u16(r); }
uint64_t mc_u64(McReader *r) { uint64_t a = mc_u32(r); return (a << 32) | mc_u32(r); }
uint64_t mc_varlong(McReader *r) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 10; i++) {
        uint8_t b = mc_u8(r);
        if (r->error || (i == 9 && (b & 254))) break;
        value |= (uint64_t)(b & 127) << (i*7);
        if (!(b & 128)) return value;
    }
    r->error = 1; return 0;
}
int mc_string(McReader *r, char *out, size_t capacity) {
    uint32_t n = mc_varint_read(r);
    if (r->error || !capacity || n >= capacity || n > r->n-r->pos) { r->error = 1; return -1; }
    memcpy(out,r->p+r->pos,n); out[n] = 0; r->pos += n; return 0;
}

double mc_f64(McReader *r) { uint64_t bits=mc_u64(r);double v;memcpy(&v,&bits,8);return v; }
float mc_f32(McReader *r) { uint32_t bits=mc_u32(r);float v;memcpy(&v,&bits,4);return v; }
McSlot mc_read_slot(McReader *r) {
    McSlot slot={-1,0};unsigned present=mc_u8(r);
    if(present>1)r->error=1;
    if(present==1){
        uint32_t item=mc_varint_read(r);unsigned count=mc_u8(r);
        if(item>INT16_MAX || !count || count>127)r->error=1;
        slot=(McSlot){(int)item,(int)count};
        if(r->pos>=r->n)r->error=1;
        else if(r->p[r->pos]==0)r->pos++;
        else if(mc_nbt_root(r,NULL)<0)r->error=1;
    }
    return slot;
}
