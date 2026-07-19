/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_QMP_H
#define QEMU_CONNECT_QMP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct qc_qmp qc_qmp_t;

/* Connect to QMP Unix socket, read greeting, execute qmp_capabilities. */
qc_qmp_t *qc_qmp_connect(const char *path);

void qc_qmp_close(qc_qmp_t *q);

/*
 * Execute a raw QMP command JSON object (without trailing newline).
 * Writes the first "return" or "error" response line into @out.
 * Skips asynchronous event messages.
 * Returns 0 on return, 1 on error object, -1 on I/O failure.
 */
int qc_qmp_execute(qc_qmp_t *q, const char *cmd_json, char *out, size_t out_len);

/* Convenience: {"execute":"quit"} — treats peer close / SHUTDOWN as success. */
int qc_qmp_quit(qc_qmp_t *q);

/* send-key with one qcode (e.g. "ret", "a", "esc", "up", "down"). */
int qc_qmp_send_key(qc_qmp_t *q, const char *qcode);

/*
 * Type a string: printable ASCII (incl. : ! shell/vi punct) + map \n to ret.
 * Does NOT auto-append Enter after the string — caller may send "ret".
 * delay_ms between keys (0 ok).
 */
int qc_qmp_type(qc_qmp_t *q, const char *text, int delay_ms);

/*
 * Type @text then optionally press Enter (ret).
 * Prefer this for shell lines so agents never leave a half-typed command.
 */
int qc_qmp_type_line(qc_qmp_t *q, const char *text, int delay_ms, bool enter);

/* Map a single char to base qcode; returns static string or NULL. */
const char *qc_qmp_char_to_qcode(int ch);

#endif
