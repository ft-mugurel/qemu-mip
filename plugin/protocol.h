/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_PROTOCOL_H
#define QEMU_CONNECT_PROTOCOL_H

#include <stddef.h>
#include <stdbool.h>

#include "queue.h"
#include "vga.h"

typedef struct {
    qc_vga_state_t *vga;
    qc_queue_t *queue;
    bool vga_refresh_enabled;
    bool socket_thread;
    bool vga_enabled;
    bool hypercall_enabled;
} qc_proto_ctx_t;

void qc_protocol_handle(const char *req_line, const qc_proto_ctx_t *ctx,
                        char *out, size_t out_len);

#endif
