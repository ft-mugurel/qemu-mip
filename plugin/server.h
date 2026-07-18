/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_SERVER_H
#define QEMU_CONNECT_SERVER_H

#include <stdbool.h>

#include "vga.h"

/* Max request line (bytes, excluding NUL). Oversized lines drop the client. */
#define QC_SERVER_MAX_LINE 8192

typedef struct qc_server qc_server_t;

/*
 * Start Unix domain listen socket.
 * @use_thread: if true, spawn a dedicated poll thread (preferred; works while
 *              the guest is idle/hlt). If false, caller must drive qc_server_poll.
 * Returns NULL on failure.
 */
qc_server_t *qc_server_start(const char *socket_path, qc_vga_state_t *vga,
                             bool use_thread);

/*
 * Non-blocking single poll iteration (accept + handle ready I/O).
 * Used when socket_thread=off, or as a no-op-safe fallback if a thread is on.
 */
void qc_server_poll(qc_server_t *srv);

/*
 * Stop order (normative):
 *   set stop → shutdown/close fds (unblocks poll) → pthread_join → free
 */
void qc_server_stop(qc_server_t *srv);

const char *qc_server_path(const qc_server_t *srv);
bool qc_server_uses_thread(const qc_server_t *srv);

#endif
