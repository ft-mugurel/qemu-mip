/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host-only unit test: LE u16 VGA cell store → character in shadow text.
 */
#include "vga.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    qc_vga_state_t vga;
    qc_vga_init(&vga);

    /* munux-style cell at phys 0xB8000: char 'A', attr 0x0F */
    uint16_t cell = (uint16_t)'A' | ((uint16_t)0x0F << 8);
    qc_vga_note_store(&vga, QEMU_CONNECT_VGA_TEXT_PHYS, cell, 2);

    /* Second cell at column 1: 'B' */
    cell = (uint16_t)'B' | ((uint16_t)0x07 << 8);
    qc_vga_note_store(&vga, QEMU_CONNECT_VGA_TEXT_PHYS + 2, cell, 2);

    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    qc_vga_snapshot_text(&vga, text, sizeof(text));

    if (text[0] != 'A' || text[1] != 'B') {
        fprintf(stderr, "FAIL: expected AB..., got '%c''%c' writes=%llu\n",
                text[0], text[1],
                (unsigned long long)qc_vga_write_count(&vga));
        qc_vga_destroy(&vga);
        return 1;
    }
    if (qc_vga_write_count(&vga) != 2) {
        fprintf(stderr, "FAIL: write_count=%llu want 2\n",
                (unsigned long long)qc_vga_write_count(&vga));
        qc_vga_destroy(&vga);
        return 1;
    }

    /* Out of range must not change count. */
    qc_vga_note_store(&vga, 0x1000, 0x41, 1);
    if (qc_vga_write_count(&vga) != 2) {
        fprintf(stderr, "FAIL: OOR store changed write_count\n");
        qc_vga_destroy(&vga);
        return 1;
    }

    printf("OK test_vga_unit: LE u16 cells → 'A''B', writes=2\n");
    qc_vga_destroy(&vga);
    return 0;
}
