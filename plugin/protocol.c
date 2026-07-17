/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "protocol.h"

#include "qemu-connect.h"

#include <stdio.h>
#include <string.h>

static int has_cmd(const char *line, const char *cmd)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"cmd\":\"%s\"", cmd);
    return strstr(line, pat) != NULL;
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
                 "{\"ok\":true,\"result\":{\"name\":\"%s\",\"proto\":\"%d.%d\"}}",
                 QEMU_CONNECT_NAME,
                 QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR);
        return;
    }

    if (has_cmd(req_line, "get_console") && vga) {
        char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
                  QEMU_CONNECT_VGA_ROWS + 8];
        qc_vga_snapshot_text(vga, text, sizeof(text));
        /* Minimal JSON string escape for control chars only; v0.1. */
        snprintf(out, out_len,
                 "{\"ok\":true,\"result\":{\"cols\":%d,\"rows\":%d,"
                 "\"writes\":%llu,\"text_len\":%zu}}",
                 QEMU_CONNECT_VGA_COLS, QEMU_CONNECT_VGA_ROWS,
                 (unsigned long long)vga->write_count, strlen(text));
        /* Full text payload lands in a follow-up revision (binary frame). */
        (void)text;
        return;
    }

    snprintf(out, out_len,
             "{\"ok\":false,\"error\":\"unknown or unimplemented cmd\"}");
}
