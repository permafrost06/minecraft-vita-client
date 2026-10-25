#include "protocol.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
size_t mc_varint_write(uint8_t *p, uint32_t v) {
    size_t n = 0;
    do { uint8_t b = v & 127; v >>= 7; p[n++] = b | (v ? 128 : 0); } while (v);
    return n;
}
uint32_t mc_varint_read(McReader *r) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 5; i++) {
        if (r->pos >= r->n) break;
        uint8_t b = r->p[r->pos++];
        if (i == 4 && (b & 0xf0)) break;
        v |= (uint32_t)(b & 127) << (7*i);
        if (!(b & 128)) return v;
    }
    r->error = 1; return 0;
}
static int fail(McClient *c, const char *message) {
    snprintf(c->message, sizeof(c->message), "%s", message);
    mc_close(c); return -1;
}
void mc_init(McClient *c) { memset(c, 0, sizeof(*c)); c->fd = -1; c->threshold = -1; }
void mc_close(McClient *c) { net_close(c->fd); c->fd = -1; c->play = 0; }
static int send_packet(McClient *c, const uint8_t *p, size_t n) {
    uint8_t prefix[10]; size_t plen = 0;
    uint8_t *encoded = NULL;
    const uint8_t *data = p; size_t len = n;
    if (c->threshold >= 0) {
        if (n >= (size_t)c->threshold) {
            uLongf compressed = compressBound(n);
            encoded = malloc(compressed);
            if (!encoded) return fail(c, "Out of memory encoding packet");
            if (compress2(encoded, &compressed, p, n, Z_DEFAULT_COMPRESSION) != Z_OK) {
                free(encoded); return fail(c, "Compression failed");
            }
            data = encoded; len = compressed;
            plen = mc_varint_write(prefix+5, n);
        } else plen = mc_varint_write(prefix+5, 0);
    }
    size_t head = mc_varint_write(prefix, len+plen);
    uint64_t deadline = net_millis()+5000;
    int r = net_write(c->fd, prefix, head, deadline);
    if (!r && plen) r = net_write(c->fd, prefix+5, plen, deadline);
    if (!r) r = net_write(c->fd, data, len, deadline);
    free(encoded);
    return r < 0 ? fail(c, "Send failed or timed out") : 0;
}
static int recv_packet(McClient *c, uint8_t **data, size_t *size) {
    uint8_t header[5]; size_t h = 0;
    uint64_t deadline = net_millis()+5000;
    do {
        if (h == 5) return fail(c, "Invalid packet length VarInt");
        if (net_read(c->fd, header+h, 1, deadline) < 0) return fail(c, "Connection closed or receive timed out");
    } while (header[h++] & 128);
    McReader reader = {header, h, 0, 0};
    uint32_t n = mc_varint_read(&reader);
    if (reader.error || !n || n > MC_MAX_PACKET) return fail(c, "Invalid or oversized packet");
    uint8_t *raw = malloc(n);
    if (!raw) return fail(c, "Out of memory receiving packet");
    if (net_read(c->fd, raw, n, deadline) < 0) { free(raw); return fail(c, "Truncated packet or receive timeout"); }
    if (c->threshold >= 0) {
        reader = (McReader){raw, n, 0, 0};
        uint32_t unpacked = mc_varint_read(&reader);
        if (reader.error || unpacked > MC_MAX_PACKET || reader.pos == n) {
            free(raw); return fail(c, "Invalid compressed packet length");
        }
        if (unpacked) {
            if (unpacked < (uint32_t)c->threshold) { free(raw); return fail(c, "Compressed packet below threshold"); }
            uint8_t *decoded = malloc(unpacked);
            if (!decoded) { free(raw); return fail(c, "Out of memory decompressing packet"); }
            z_stream stream; memset(&stream, 0, sizeof(stream));
            stream.next_in = raw+reader.pos; stream.avail_in = n-reader.pos;
            stream.next_out = decoded; stream.avail_out = unpacked;
            int r = inflateInit(&stream);
            if (r == Z_OK) {
                r = inflate(&stream, Z_FINISH);
                int valid = r == Z_STREAM_END && stream.total_out == unpacked && stream.avail_in == 0;
                inflateEnd(&stream);
                r = valid ? Z_OK : Z_DATA_ERROR;
            }
            free(raw);
            if (r != Z_OK) { free(decoded); return fail(c, "Invalid zlib packet"); }
            raw = decoded; n = unpacked;
        } else {
            n -= reader.pos;
            if (n >= (uint32_t)c->threshold) { free(raw); return fail(c, "Uncompressed packet exceeds threshold"); }
            memmove(raw, raw+reader.pos, n);
        }
    }
    *data = raw; *size = n; c->packets++; return 0;
}
static size_t put_string(uint8_t *p, const char *s) {
    size_t n = strlen(s), h = mc_varint_write(p, n); memcpy(p+h, s, n); return h+n;
}
static int get_string(McReader *r, char *out, size_t capacity) {
    uint32_t n = mc_varint_read(r);
    if (r->error || n > r->n-r->pos || !capacity) { r->error = 1; return -1; }
    size_t copy = n < capacity-1 ? n : capacity-1;
    /* Strip controls before text reaches a terminal/debug-screen renderer. */
    for (size_t i = 0; i < copy; i++) out[i] = r->p[r->pos+i] < 32 ? ' ' : r->p[r->pos+i];
    out[copy] = 0; r->pos += n; return 0;
}
static int skip_string(McReader *r) {
    uint32_t n = mc_varint_read(r);
    if (r->error || n > r->n-r->pos) return -1;
    r->pos += n; return 0;
}
static int read_properties(McReader *r) {
    uint32_t count = mc_varint_read(r);
    if (r->error || count > 1024) return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (skip_string(r) < 0 || skip_string(r) < 0 || r->pos >= r->n) return -1;
        uint8_t signed_property = r->p[r->pos++];
        if (signed_property > 1 || (signed_property && skip_string(r) < 0)) return -1;
    }
    return 0;
}
static int connect_state(McClient *c, const char *ip, uint16_t port, int state) {
    mc_close(c); mc_init(c);
    if (!port || strlen(ip) > 255) return fail(c, "Invalid server address");
    c->fd = net_connect(ip, port);
    if (c->fd < 0) return fail(c, "Cannot connect (IPv4 only); check Wi-Fi, address and port");
    uint8_t p[300]; size_t n = 0;
    p[n++] = 0; n += mc_varint_write(p+n, MC_PROTOCOL); n += put_string(p+n, ip);
    p[n++] = port >> 8; p[n++] = port & 255; p[n++] = state;
    return send_packet(c, p, n);
}
int mc_status(McClient *c, const char *ip, uint16_t port, char *json, size_t capacity, unsigned *ping) {
    if (connect_state(c, ip, port, 1) < 0) return -1;
    uint8_t request = 0, *p; size_t n;
    if (send_packet(c, &request, 1) < 0 || recv_packet(c, &p, &n) < 0) return -1;
    McReader r = {p, n, 0, 0}; uint32_t id = mc_varint_read(&r);
    int ok = id == 0 && get_string(&r, json, capacity) == 0 && r.pos == n;
    free(p);
    if (!ok) return fail(c, "Invalid server status response");
    uint8_t echo[9] = {1}; uint64_t start = net_millis();
    for (int i = 0; i < 8; i++) echo[8-i] = (uint8_t)(start >> (i*8));
    if (send_packet(c, echo, sizeof(echo)) < 0 || recv_packet(c, &p, &n) < 0) return -1;
    ok = n == sizeof(echo) && memcmp(echo, p, n) == 0; free(p);
    if (!ok) return fail(c, "Server returned an invalid pong");
    *ping = (unsigned)(net_millis()-start);
    snprintf(c->message, sizeof(c->message), "Status and ping OK (%u ms)", *ping);
    mc_close(c); return 0;
}
int mc_login(McClient *c, const char *ip, uint16_t port, const char *username) {
    size_t len = strlen(username);
    if (!len || len > 16) return fail(c, "Username must contain 1-16 letters, digits or underscores");
    for (size_t i = 0; i < len; i++) {
        char b = username[i];
        if (!((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'))
            return fail(c, "Invalid username");
    }
    if (connect_state(c, ip, port, 2) < 0) return -1;
    uint8_t p[32] = {0}; size_t n = 1+put_string(p+1, username);
    p[n++] = 0; /* No optional player UUID; offline-mode server derives it. */
    if (send_packet(c, p, n) < 0) return -1;
    snprintf(c->message, sizeof(c->message), "Login requested as %s", username); return 0;
}
static double get_double(McReader *r) {
    if (r->n-r->pos < 8) { r->error = 1; return 0; }
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) bits = (bits << 8) | r->p[r->pos++];
    double value; memcpy(&value, &bits, 8); return value;
}
static float get_float(McReader *r) {
    if (r->n-r->pos < 4) { r->error = 1; return 0; }
    uint32_t bits = 0;
    for (int i = 0; i < 4; i++) bits = (bits << 8) | r->p[r->pos++];
    float value; memcpy(&value, &bits, 4); return value;
}
static void put_number(uint8_t *p, const void *value, size_t n) {
    uint64_t bits = 0;
    if (n == 4) { uint32_t small; memcpy(&small, value, 4); bits = small; }
    else memcpy(&bits, value, 8);
    for (size_t i = 0; i < n; i++) p[n-1-i] = (uint8_t)(bits >> (i*8));
}
int mc_step(McClient *c) {
    uint8_t *p; size_t n;
    if (c->fd < 0) return -1;
    if (!net_ready(c->fd, 20)) return 0;
    if (recv_packet(c, &p, &n) < 0) return -1;
    McReader r = {p, n, 0, 0}; uint32_t id = mc_varint_read(&r);
    int result = 0;
    if (r.error) { free(p); return fail(c, "Missing packet ID"); }
    if ((!c->play && id == 0) || (c->play && id == 0x1a)) {
        char reason[700];
        if (get_string(&r, reason, sizeof(reason)) < 0) result = fail(c, "Invalid disconnect reason");
        else { snprintf(c->message, sizeof(c->message), "Disconnected: %s", reason); mc_close(c); result = -1; }
    } else if (!c->play) {
        if (id == 1) result = fail(c, "Server requires Microsoft authentication; use a private online-mode=false test server for now");
        else if (id == 3) {
            uint32_t threshold = mc_varint_read(&r);
            if (r.error || r.pos != n || threshold > MC_MAX_PACKET) result = fail(c, "Invalid compression threshold");
            else c->threshold = (int)threshold;
        } else if (id == 2) {
            /* UUID, username and bounded profile properties. */
            char username[17];
            if (n-r.pos < 16) result = fail(c, "Invalid login success");
            else {
                r.pos += 16;
                if (get_string(&r, username, sizeof(username)) < 0 || read_properties(&r) < 0 || r.pos != n) result = fail(c, "Invalid login username");
                else {
                    c->play = 1;
                    snprintf(c->message, sizeof(c->message), "Logged in as %s; waiting for spawn", username);
                }
            }
        } else result = fail(c, "Unexpected login packet (wrong server version?)");
    } else if (id == 0x28) {
        c->joined = 1;
        /* Wait for Join Game before sending play packets. Login Success can
           arrive before vanilla's receiving connection has switched state. */
        uint8_t settings[] = {0x08, 5, 'e','n','_','u','s', 2, 0, 1, 0x7f, 1, 0, 1};
        result = send_packet(c, settings, sizeof(settings));
        if (!result) snprintf(c->message, sizeof(c->message), "Joined server. Terrain rendering is the next milestone.");
    } else if (id == 0x24) c->chunks++;
    else if (id == 0x23) {
        if (n-r.pos != 8) result = fail(c, "Invalid keepalive");
        else { uint8_t reply[9] = {0x12}; memcpy(reply+1, p+r.pos, 8); result = send_packet(c, reply, 9); c->keepalives++; }
    } else if (id == 0x32) {
        if (n-r.pos != 4) result = fail(c, "Invalid play ping");
        else { uint8_t pong[5] = {0x20}; memcpy(pong+1, p+r.pos, 4); result = send_packet(c, pong, 5); }
    } else if (id == 0x3c) {
        double x = get_double(&r), y = get_double(&r), z = get_double(&r);
        float yaw = get_float(&r), pitch = get_float(&r);
        uint8_t flags = 0;
        if (r.pos >= n) r.error = 1; else flags = p[r.pos++];
        uint32_t teleport = mc_varint_read(&r);
        if (r.error || r.pos != n || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(yaw) || !isfinite(pitch)) result = fail(c, "Invalid teleport packet");
        else {
            c->x = x + ((flags & 1) ? c->x : 0); c->y = y + ((flags & 2) ? c->y : 0); c->z = z + ((flags & 4) ? c->z : 0);
            c->yaw = yaw + ((flags & 8) ? c->yaw : 0); c->pitch = pitch + ((flags & 16) ? c->pitch : 0);
            uint8_t reply[40] = {0}; size_t size = 1+mc_varint_write(reply+1, teleport);
            result = send_packet(c, reply, size);
            if (!result) {
                reply[0] = 0x15;
                put_number(reply+1, &c->x, 8); put_number(reply+9, &c->y, 8); put_number(reply+17, &c->z, 8);
                put_number(reply+25, &c->yaw, 4); put_number(reply+29, &c->pitch, 4); reply[33] = 0;
                result = send_packet(c, reply, 34); c->teleports++;
            }
        }
    }
    free(p); return result < 0 ? -1 : 1;
}
