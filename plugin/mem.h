/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_MEM_H
#define QEMU_CONNECT_MEM_H

#include <qemu-plugin.h>
#include <stdbool.h>
#include <stdint.h>

#include "vga.h"

void qc_mem_instrument_tb(struct qemu_plugin_tb *tb, qc_vga_state_t *vga,
                          bool hypercall_on);

uint64_t qc_mem_cb_fired(void);
uint64_t qc_mem_cb_vga_hit(void);

/* Discontinuity counters (exceptions / interrupts / hostcalls). */
void qc_discon_init(void);
void qc_discon_on_event(int type_flags);
uint64_t qc_discon_exception(void);
uint64_t qc_discon_interrupt(void);
uint64_t qc_discon_hostcall(void);
uint64_t qc_discon_total(void);

#endif
