/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_PROTOCOL_H
#define QEMU_CONNECT_PROTOCOL_H

#include <stddef.h>

#include "vga.h"

/*
 * Line-oriented JSON-ish protocol (v0.1), one request/response per line.
 * See docs/protocol.md.
 *
 * request:  {"cmd":"ping"}
 * response: {"ok":true,"result":{"pong":true,"proto":"0.1"}}
 */

/* Handle one request line; write response into out (NUL-terminated). */
void qc_protocol_handle(const char *req_line, qc_vga_state_t *vga,
                        char *out, size_t out_len);

#endif
