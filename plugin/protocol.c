/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"

#include "qemu-connect.h"

#include <stdio.h>
#include <string.h>

__attribute__((weak)) uint64_t qc_mem_cb_fired(void)
{
    return 0;
}
__attribute__((weak)) uint64_t qc_mem_cb_vga_hit(void)
{
    return 0;
}

static int has_cmd(const char *line, const char *cmd)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"cmd\":\"%s\"", cmd);
    return strstr(line, pat) != NULL;
}

static bool has_refresh_true(const char *line)
{
    if (!line) {
        return false;
    }
    if (strstr(line, "\"refresh\":true") || strstr(line, "\"refresh\": true")) {
        return true;
    }
    if (strstr(line, "\"refresh\":1")) {
        return true;
    }
    return false;
}

static const char *hw_code_name(int code)
{
    switch (code) {
    case 0:
        return "OK";
    case 1:
        return "ERROR";
    case 2:
        return "DEVICE_ERROR";
    case 3:
        return "ACCESS_DENIED";
    case 4:
        return "INVALID_ADDRESS";
    case 5:
        return "INVALID_ADDRESS_SPACE";
    default:
        return "UNKNOWN";
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
        case '"':
            esc = "\\\"";
            break;
        case '\\':
            esc = "\\\\";
            break;
        case '\b':
            esc = "\\b";
            break;
        case '\f':
            esc = "\\f";
            break;
        case '\n':
            esc = "\\n";
            break;
        case '\r':
            esc = "\\r";
            break;
        case '\t':
            esc = "\\t";
            break;
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

static void emit_console(const qc_proto_ctx_t *ctx, const char *source, char *out,
                         size_t out_len)
{
    qc_vga_state_t *vga = ctx ? ctx->vga : NULL;
    if (!vga) {
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"vga unavailable\"}");
        return;
    }

    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    size_t text_len = qc_vga_snapshot_text(vga, text, sizeof(text));
    uint64_t writes = qc_vga_write_count(vga);

    char esc[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS * 6 + 512];
    if (json_escape(text, esc, sizeof(esc)) == (size_t)-1) {
        snprintf(out, out_len,
                 "{\"ok\":false,\"error\":\"console escape overflow\"}");
        return;
    }

    int n = snprintf(
        out, out_len,
        "{\"ok\":true,\"result\":{"
        "\"cols\":%d,\"rows\":%d,"
        "\"writes\":%llu,\"text_len\":%zu,"
        "\"source\":\"%s\","
        "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
        "\"text\":\"%s\""
        "}}",
        QEMU_CONNECT_VGA_COLS, QEMU_CONNECT_VGA_ROWS,
        (unsigned long long)writes, text_len, source,
        (unsigned long long)qc_mem_cb_fired(),
        (unsigned long long)qc_mem_cb_vga_hit(), esc);

    if (n < 0 || (size_t)n >= out_len) {
        snprintf(out, out_len,
                 "{\"ok\":false,\"error\":\"console response too large\"}");
    }
}

void qc_protocol_handle(const char *req_line, const qc_proto_ctx_t *ctx,
                        char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    if (!req_line || req_line[0] == '\0') {
        snprintf(out, out_len,
                 "{\"ok\":false,\"error\":\"empty request\"}");
        return;
    }

    if (has_cmd(req_line, "ping") || strstr(req_line, "\"ping\"")) {
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"pong\":true,\"name\":\"%s\","
                 "\"proto\":\"%d.%d\"}}",
                 QEMU_CONNECT_NAME,
                 QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR);
        return;
    }

    if (has_cmd(req_line, "version")) {
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"name\":\"%s\",\"proto\":\"%d.%d\","
                 "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
                 "\"vga_refresh\":%s,\"queue_timeout_ms\":%d}}",
                 QEMU_CONNECT_NAME,
                 QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR,
                 (unsigned long long)qc_mem_cb_fired(),
                 (unsigned long long)qc_mem_cb_vga_hit(),
                 (ctx && ctx->vga_refresh_enabled) ? "true" : "false",
                 ctx && ctx->queue ? qc_queue_timeout_ms(ctx->queue)
                                   : QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT);
        return;
    }

    if (has_cmd(req_line, "get_console")) {
        if (!ctx || !ctx->vga) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"vga unavailable\"}");
            return;
        }

        if (has_refresh_true(req_line)) {
            if (!ctx->vga_refresh_enabled) {
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"refresh disabled\"}");
                return;
            }
            if (!ctx->queue) {
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"queue unavailable\"}");
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
                snprintf(out, out_len,
                         "{\"ok\":false,\"error\":\"refresh failed\"}");
                return;
            }
            emit_console(ctx, "refresh", out, out_len);
            return;
        }

        /* Default: shadow-only — never blocks on vCPU. */
        emit_console(ctx, "shadow", out, out_len);
        return;
    }

    snprintf(out, out_len,
             "{\"ok\":false,\"error\":\"unknown or unimplemented cmd\"}");
}
