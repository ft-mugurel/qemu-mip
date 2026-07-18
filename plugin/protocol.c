/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"

#include "hypercall.h"
#include "mem.h"
#include "qemu-connect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((weak)) uint64_t qc_mem_cb_fired(void) { return 0; }
__attribute__((weak)) uint64_t qc_mem_cb_vga_hit(void) { return 0; }
__attribute__((weak)) uint64_t qc_discon_exception(void) { return 0; }
__attribute__((weak)) uint64_t qc_discon_interrupt(void) { return 0; }
__attribute__((weak)) uint64_t qc_discon_hostcall(void) { return 0; }
__attribute__((weak)) uint64_t qc_discon_total(void) { return 0; }

static int has_cmd(const char *line, const char *cmd)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"cmd\":\"%s\"", cmd);
    return strstr(line, pat) != NULL;
}

static bool has_refresh_true(const char *line)
{
    return line && (strstr(line, "\"refresh\":true") ||
                    strstr(line, "\"refresh\": true") ||
                    strstr(line, "\"refresh\":1"));
}

static const char *hw_code_name(int code)
{
    switch (code) {
    case 0: return "OK";
    case 1: return "ERROR";
    case 2: return "DEVICE_ERROR";
    case 3: return "ACCESS_DENIED";
    case 4: return "INVALID_ADDRESS";
    case 5: return "INVALID_ADDRESS_SPACE";
    default: return "UNKNOWN";
    }
}

static size_t json_escape(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return (size_t)-1;
    }
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        const char *esc = NULL;
        char tmp[8];
        switch (*p) {
        case '"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default:
            if (*p < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
                esc = tmp;
            }
            break;
        }
        if (esc) {
            size_t el = strlen(esc);
            if (o + el + 1 > dst_len) {
                return (size_t)-1;
            }
            memcpy(dst + o, esc, el);
            o += el;
        } else {
            if (o + 2 > dst_len) {
                return (size_t)-1;
            }
            dst[o++] = (char)*p;
        }
    }
    dst[o] = '\0';
    return o;
}

static bool parse_u64_field(const char *line, const char *key, uint64_t *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ') {
        p++;
    }
    char *end = NULL;
    unsigned long long v = strtoull(p, &end, 0);
    if (end == p) {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static void emit_console(const qc_proto_ctx_t *ctx, const char *source, char *out,
                         size_t out_len)
{
    if (!ctx || !ctx->vga) {
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"vga unavailable\"}");
        return;
    }
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    size_t text_len = qc_vga_snapshot_text(ctx->vga, text, sizeof(text));
    uint64_t writes = qc_vga_write_count(ctx->vga);
    char esc[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS * 6 + 512];
    if (json_escape(text, esc, sizeof(esc)) == (size_t)-1) {
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"console escape overflow\"}");
        return;
    }
    int n = snprintf(out, out_len,
                     "{\"ok\":true,\"result\":{"
                     "\"cols\":%d,\"rows\":%d,\"writes\":%llu,\"text_len\":%zu,"
                     "\"source\":\"%s\",\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
                     "\"text\":\"%s\"}}",
                     QEMU_CONNECT_VGA_COLS, QEMU_CONNECT_VGA_ROWS,
                     (unsigned long long)writes, text_len, source,
                     (unsigned long long)qc_mem_cb_fired(),
                     (unsigned long long)qc_mem_cb_vga_hit(), esc);
    if (n < 0 || (size_t)n >= out_len) {
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"console response too large\"}");
    }
}

static void hex_encode(const uint8_t *data, size_t len, char *out, size_t out_len)
{
    static const char *H = "0123456789abcdef";
    if (out_len < len * 2 + 1) {
        if (out_len) {
            out[0] = '\0';
        }
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = H[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = H[data[i] & 0xf];
    }
    out[len * 2] = '\0';
}

void qc_protocol_handle(const char *req_line, const qc_proto_ctx_t *ctx,
                        char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!req_line || !req_line[0]) {
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"empty request\"}");
        return;
    }

    if (has_cmd(req_line, "ping") || strstr(req_line, "\"ping\"")) {
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"pong\":true,\"name\":\"%s\","
                 "\"proto\":\"%d.%d\"}}",
                 QEMU_CONNECT_NAME, QEMU_CONNECT_PROTO_MAJOR,
                 QEMU_CONNECT_PROTO_MINOR);
        return;
    }

    if (has_cmd(req_line, "version")) {
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"name\":\"%s\",\"proto\":\"%d.%d\","
                 "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
                 "\"vga_refresh\":%s,\"queue_timeout_ms\":%d}}",
                 QEMU_CONNECT_NAME, QEMU_CONNECT_PROTO_MAJOR,
                 QEMU_CONNECT_PROTO_MINOR,
                 (unsigned long long)qc_mem_cb_fired(),
                 (unsigned long long)qc_mem_cb_vga_hit(),
                 (ctx && ctx->vga_refresh_enabled) ? "true" : "false",
                 ctx && ctx->queue ? qc_queue_timeout_ms(ctx->queue)
                                   : QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT);
        return;
    }

    if (has_cmd(req_line, "status")) {
        qc_hypercall_state_t hc;
        qc_hypercall_snapshot(&hc);
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{"
                 "\"name\":\"%s\",\"proto\":\"%d.%d\","
                 "\"socket_thread\":%s,\"vga\":%s,\"vga_refresh\":%s,"
                 "\"hypercall\":%s,"
                 "\"vga_writes\":%llu,"
                 "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
                 "\"discon\":{\"exception\":%llu,\"interrupt\":%llu,"
                 "\"hostcall\":%llu,\"total\":%llu},"
                 "\"agent\":{\"count\":%llu,\"last_cmd\":%u,\"last_status\":%u,"
                 "\"last_name\":\"%s\",\"pending_event\":%s}"
                 "}}",
                 QEMU_CONNECT_NAME, QEMU_CONNECT_PROTO_MAJOR,
                 QEMU_CONNECT_PROTO_MINOR,
                 (ctx && ctx->socket_thread) ? "true" : "false",
                 (ctx && ctx->vga_enabled) ? "true" : "false",
                 (ctx && ctx->vga_refresh_enabled) ? "true" : "false",
                 (ctx && ctx->hypercall_enabled) ? "true" : "false",
                 (unsigned long long)(ctx && ctx->vga ? qc_vga_write_count(ctx->vga)
                                                      : 0),
                 (unsigned long long)qc_mem_cb_fired(),
                 (unsigned long long)qc_mem_cb_vga_hit(),
                 (unsigned long long)qc_discon_exception(),
                 (unsigned long long)qc_discon_interrupt(),
                 (unsigned long long)qc_discon_hostcall(),
                 (unsigned long long)qc_discon_total(),
                 (unsigned long long)hc.count, hc.last_cmd, hc.last_status,
                 hc.last_name[0] ? hc.last_name : "",
                 hc.have_event ? "true" : "false");
        return;
    }

    if (has_cmd(req_line, "get_agent_event")) {
        qc_hypercall_state_t hc;
        if (!qc_hypercall_take_event(&hc)) {
            snprintf(out, out_len,
                     "{\"ok\":true,\"result\":{\"event\":null}}");
            return;
        }
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"event\":{"
                 "\"name\":\"%s\",\"cmd\":%u,\"status\":%u,\"count\":%llu}}}",
                 hc.last_name, hc.last_cmd, hc.last_status,
                 (unsigned long long)hc.count);
        return;
    }

    if (has_cmd(req_line, "get_console")) {
        if (!ctx || !ctx->vga) {
            snprintf(out, out_len, "{\"ok\":false,\"error\":\"vga unavailable\"}");
            return;
        }
        if (has_refresh_true(req_line)) {
            if (!ctx->vga_refresh_enabled || !ctx->queue) {
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"refresh disabled\"}");
                return;
            }
            qc_queue_result_t qr =
                qc_queue_request_vga_refresh(ctx->queue, ctx->vga, -1);
            if (qr.status == QC_QUEUE_TIMEOUT || qr.status == QC_QUEUE_BUSY) {
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"vcpu_idle_timeout\"}");
                return;
            }
            if (qr.status == QC_QUEUE_HW_FAIL) {
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"hwaddr_read_failed\","
                         "\"result\":{\"code\":\"%s\",\"code_num\":%d}}",
                         hw_code_name(qr.hw_code), qr.hw_code);
                return;
            }
            if (qr.status != QC_QUEUE_OK) {
                snprintf(out, out_len, "{\"ok\":false,\"error\":\"refresh failed\"}");
                return;
            }
            emit_console(ctx, "refresh", out, out_len);
            return;
        }
        emit_console(ctx, "shadow", out, out_len);
        return;
    }

    if (has_cmd(req_line, "mem_read")) {
        if (!ctx || !ctx->queue) {
            snprintf(out, out_len, "{\"ok\":false,\"error\":\"queue unavailable\"}");
            return;
        }
        uint64_t phys = 0, len64 = 0;
        if (!parse_u64_field(req_line, "phys", &phys) ||
            !parse_u64_field(req_line, "len", &len64)) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"mem_read requires phys and len\"}");
            return;
        }
        if (len64 == 0 || len64 > QEMU_CONNECT_MEM_READ_MAX) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"len out of range (1..%d)\"}",
                     QEMU_CONNECT_MEM_READ_MAX);
            return;
        }
        size_t len = (size_t)len64;
        uint8_t *buf = malloc(len);
        if (!buf) {
            snprintf(out, out_len, "{\"ok\":false,\"error\":\"oom\"}");
            return;
        }
        qc_queue_result_t qr =
            qc_queue_request_mem_read(ctx->queue, phys, len, buf, -1);
        if (qr.status == QC_QUEUE_TIMEOUT || qr.status == QC_QUEUE_BUSY) {
            free(buf);
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"vcpu_idle_timeout\"}");
            return;
        }
        if (qr.status != QC_QUEUE_OK) {
            free(buf);
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"hwaddr_read_failed\","
                     "\"result\":{\"code\":\"%s\",\"code_num\":%d}}",
                     hw_code_name(qr.hw_code), qr.hw_code);
            return;
        }
        char *hex = malloc(qr.data_len * 2 + 1);
        if (!hex) {
            free(buf);
            snprintf(out, out_len, "{\"ok\":false,\"error\":\"oom\"}");
            return;
        }
        hex_encode(buf, qr.data_len, hex, qr.data_len * 2 + 1);
        int n = snprintf(out, out_len,
                         "{\"ok\":true,\"result\":{"
                         "\"phys\":%llu,\"len\":%zu,\"hex\":\"%s\"}}",
                         (unsigned long long)phys, qr.data_len, hex);
        free(hex);
        free(buf);
        if (n < 0 || (size_t)n >= out_len) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"mem_read response too large\"}");
        }
        return;
    }

    snprintf(out, out_len,
             "{\"ok\":false,\"error\":\"unknown or unimplemented cmd\"}");
}
