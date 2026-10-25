#ifndef MC_READER_H
#define MC_READER_H
#include "protocol.h"
uint8_t mc_u8(McReader *r);
uint16_t mc_u16(McReader *r);
uint32_t mc_u32(McReader *r);
uint64_t mc_u64(McReader *r);
uint64_t mc_varlong(McReader *r);
int mc_skip(McReader *r, size_t count);
int mc_string(McReader *r, char *out, size_t capacity);
#endif
