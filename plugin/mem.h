/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_MEM_H
#define QEMU_CONNECT_MEM_H

#include <qemu-plugin.h>
#include <stdbool.h>
#include <stdint.h>

#include "vga.h"

/* Register store callbacks on every insn in @tb (when vga scrape is enabled). */
void qc_mem_instrument_tb(struct qemu_plugin_tb *tb, qc_vga_state_t *vga);

uint64_t qc_mem_cb_fired(void);
uint64_t qc_mem_cb_vga_hit(void);

#endif
