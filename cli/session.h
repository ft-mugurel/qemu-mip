/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_SESSION_H
#define QEMU_CONNECT_SESSION_H

/* Persistent guest session: start once, many cmds, then stop. */
int qc_cmd_session(int argc, char **argv);

#endif
