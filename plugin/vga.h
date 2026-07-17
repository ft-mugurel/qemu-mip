/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_VGA_H
#define QEMU_CONNECT_VGA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "qemu-connect.h"

/* Shadow of the classic 80x25 VGA text buffer (char bytes only). */
typedef struct {
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS];
    uint64_t write_count;
    bool dirty;
} qc_vga_state_t;

void qc_vga_init(qc_vga_state_t *s);
void qc_vga_note_store(qc_vga_state_t *s, uint64_t hwaddr, uint64_t value,
                       unsigned size);
/* Copy current text into out (NUL-terminated, rows separated by '\n'). */
size_t qc_vga_snapshot_text(const qc_vga_state_t *s, char *out, size_t out_len);

#endif
