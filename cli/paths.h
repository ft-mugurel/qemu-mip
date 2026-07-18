/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_PATHS_H
#define QEMU_CONNECT_PATHS_H

/* Resolved once per process; return static storage. */
const char *qc_install_root(void);   /* PREFIX/share/qemu-connect or repo */
const char *qc_default_plugin(void);
const char *qc_default_iso(void);
const char *qc_default_disk(void);
const char *qc_default_cli_hint(void);

#endif
