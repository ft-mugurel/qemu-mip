/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"

#include "qemu-connect.h"

#include <stdio.h>
#include <string.h>

/* Optional mem stats when linked into the plugin (weak stubs for unit tests). */
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

/*
 * JSON-escape @src into @dst. Returns bytes written excluding NUL, or (size_t)-1
 * if @dst is too small.
 */
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

void qc_protocol_handle(const char *req_line, qc_vga_state_t *vga,
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
                 "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu}}",
                 QEMU_CONNECT_NAME,
                 QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR,
                 (unsigned long long)qc_mem_cb_fired(),
                 (unsigned long long)qc_mem_cb_vga_hit());
        return;
    }

    if (has_cmd(req_line, "get_console")) {
        if (!vga) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"vga unavailable\"}");
            return;
        }

        /* 80*25 + newlines + NUL */
        char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
                  QEMU_CONNECT_VGA_ROWS + 8];
        size_t text_len = qc_vga_snapshot_text(vga, text, sizeof(text));
        uint64_t writes = qc_vga_write_count(vga);

        /* Escaped text fits in worst case ~6x for \u00xx; budget generously. */
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
            "\"source\":\"shadow\","
            "\"mem_cb_fired\":%llu,\"mem_cb_vga_hit\":%llu,"
            "\"text\":\"%s\""
            "}}",
            QEMU_CONNECT_VGA_COLS, QEMU_CONNECT_VGA_ROWS,
            (unsigned long long)writes, text_len,
            (unsigned long long)qc_mem_cb_fired(),
            (unsigned long long)qc_mem_cb_vga_hit(), esc);

        if (n < 0 || (size_t)n >= out_len) {
            snprintf(out, out_len,
                     "{\"ok\":false,\"error\":\"console response too large\"}");
        }
        return;
    }

    snprintf(out, out_len,
             "{\"ok\":false,\"error\":\"unknown or unimplemented cmd\"}");
}
