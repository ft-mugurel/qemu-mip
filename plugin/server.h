/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_SERVER_H
#define QEMU_CONNECT_SERVER_H

#include <stdbool.h>

#include "protocol.h"

#define QC_SERVER_MAX_LINE 8192

typedef struct qc_server qc_server_t;

/*
 * Start Unix domain listen socket.
 * @ctx must remain valid for the lifetime of the server (plugin globals).
 */
qc_server_t *qc_server_start(const char *socket_path, const qc_proto_ctx_t *ctx,
                             bool use_thread);

void qc_server_poll(qc_server_t *srv);
void qc_server_stop(qc_server_t *srv);

const char *qc_server_path(const qc_server_t *srv);
bool qc_server_uses_thread(const qc_server_t *srv);

#endif
