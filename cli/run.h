/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_RUN_H
#define QEMU_CONNECT_RUN_H

#define QC_RUN_OK              0
#define QC_RUN_EXPECT_FAIL     1
#define QC_RUN_ISO_MISSING     2
#define QC_RUN_QEMU_CRASH      3
#define QC_RUN_CONNECT_FAIL    4
#define QC_RUN_USAGE           2

int qc_cmd_run(int argc, char **argv);
/* Simple guest helper: guest [shell-command...] */
int qc_cmd_guest(int argc, char **argv);

#endif
