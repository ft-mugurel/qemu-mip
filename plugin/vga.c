/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vga.h"

#include <string.h>

void qc_vga_init(qc_vga_state_t *s)
{
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < sizeof(s->text); i++) {
        s->text[i] = ' ';
    }
}

void qc_vga_note_store(qc_vga_state_t *s, uint64_t hwaddr, uint64_t value,
                       unsigned size)
{
    if (hwaddr < QEMU_CONNECT_VGA_TEXT_PHYS ||
        hwaddr >= QEMU_CONNECT_VGA_TEXT_PHYS + QEMU_CONNECT_VGA_BYTES) {
        return;
    }

    uint64_t off = hwaddr - QEMU_CONNECT_VGA_TEXT_PHYS;
    /* Only character bytes (even offsets) land in our text shadow. */
    for (unsigned i = 0; i < size; i++) {
        uint64_t b = off + i;
        if (b >= QEMU_CONNECT_VGA_BYTES) {
            break;
        }
        if ((b % 2) == 0) {
            size_t ti = (size_t)(b / 2);
            if (ti < sizeof(s->text)) {
                s->text[ti] = (char)((value >> (8 * i)) & 0xff);
                s->dirty = true;
            }
        }
    }
    s->write_count++;
}

size_t qc_vga_snapshot_text(const qc_vga_state_t *s, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return 0;
    }

    size_t n = 0;
    for (int row = 0; row < QEMU_CONNECT_VGA_ROWS; row++) {
        for (int col = 0; col < QEMU_CONNECT_VGA_COLS; col++) {
            if (n + 1 >= out_len) {
                out[n] = '\0';
                return n;
            }
            out[n++] = s->text[row * QEMU_CONNECT_VGA_COLS + col];
        }
        if (row + 1 < QEMU_CONNECT_VGA_ROWS) {
            if (n + 1 >= out_len) {
                out[n] = '\0';
                return n;
            }
            out[n++] = '\n';
        }
    }
    out[n] = '\0';
    return n;
}
