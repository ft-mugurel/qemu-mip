/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared constants for the qemu-connect agent protocol.
 * Used by the TCG plugin and host CLI.
 */
#ifndef QEMU_CONNECT_H
#define QEMU_CONNECT_H

#define QEMU_CONNECT_NAME        "qemu-connect"
#define QEMU_CONNECT_PROTO_MAJOR 0
#define QEMU_CONNECT_PROTO_MINOR 1

/* Default Unix socket path if the plugin is loaded without socket= */
#define QEMU_CONNECT_DEFAULT_SOCK "/tmp/qemu-connect.sock"

/* VGA text mode (PC BIOS / many hobby kernels) */
#define QEMU_CONNECT_VGA_TEXT_PHYS 0x000B8000ULL
#define QEMU_CONNECT_VGA_COLS      80
#define QEMU_CONNECT_VGA_ROWS      25
#define QEMU_CONNECT_VGA_CELL      2 /* char + attribute */
#define QEMU_CONNECT_VGA_BYTES \
    (QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS * QEMU_CONNECT_VGA_CELL)

/* Optional guest hypercall magic (future): store to this phys addr */
#define QEMU_CONNECT_HYPERCALL_PHYS 0xFEE1DEADULL

#endif /* QEMU_CONNECT_H */
