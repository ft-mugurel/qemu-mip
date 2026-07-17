/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_SERVER_H
#define QEMU_CONNECT_SERVER_H

#include <stdbool.h>

#include "vga.h"

typedef struct qc_server qc_server_t;

/* Start Unix domain listen socket. Returns NULL on failure. */
qc_server_t *qc_server_start(const char *socket_path, qc_vga_state_t *vga);

/* Non-blocking poll: accept + handle one request if any (stub for now). */
void qc_server_poll(qc_server_t *srv);

void qc_server_stop(qc_server_t *srv);

const char *qc_server_path(const qc_server_t *srv);

#endif
