/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_VGA_H
#define QEMU_CONNECT_VGA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "qemu-connect.h"

/* Shadow of the classic 80x25 VGA text buffer (char bytes only). */
typedef struct {
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS];
    uint64_t write_count;
    bool dirty;
    pthread_mutex_t lock;
} qc_vga_state_t;

void qc_vga_init(qc_vga_state_t *s);
void qc_vga_destroy(qc_vga_state_t *s);

/* Apply a store of @size bytes of @value (host-endian LE payload) at @hwaddr. */
void qc_vga_note_store(qc_vga_state_t *s, uint64_t hwaddr, uint64_t value,
                       unsigned size);

/*
 * Rebuild shadow from raw cell bytes (len == QEMU_CONNECT_VGA_BYTES preferred).
 * Each cell is LE u16: char in low byte. Increments write_count once.
 */
void qc_vga_load_cells(qc_vga_state_t *s, const uint8_t *raw, size_t len);

/*
 * Copy current text into out (NUL-terminated, rows separated by '\n').
 * Clears dirty. Thread-safe.
 */
size_t qc_vga_snapshot_text(qc_vga_state_t *s, char *out, size_t out_len);

uint64_t qc_vga_write_count(qc_vga_state_t *s);

#endif
