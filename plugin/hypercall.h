/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_HYPERCALL_H
#define QEMU_CONNECT_HYPERCALL_H

#include <stdbool.h>
#include <stdint.h>

#include "qemu-connect.h"

typedef struct {
    uint32_t magic;
    uint32_t cmd;
    uint32_t status;
    uint32_t reserved;
} qc_hypercall_msg_t;

typedef struct {
    uint64_t count;
    uint32_t last_cmd;
    uint32_t last_status;
    bool have_event;
    char last_name[32]; /* READY / EXIT / LOG / UNKNOWN */
} qc_hypercall_state_t;

void qc_hypercall_init(void);
void qc_hypercall_note_store(uint64_t phys, uint64_t value, unsigned size);
void qc_hypercall_snapshot(qc_hypercall_state_t *out);
/* Pop one-shot "have_event" for get_agent_event. */
bool qc_hypercall_take_event(qc_hypercall_state_t *out);

#endif
