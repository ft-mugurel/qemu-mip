/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hypercall.h"
#include "qemu-connect.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    qc_hypercall_init();

    /* Build LE message: magic, cmd=EXIT(2), status=7, reserved=0 */
    uint32_t words[4] = {
        QEMU_CONNECT_HYPERCALL_MAGIC,
        QEMU_CONNECT_HC_EXIT,
        7u,
        0u,
    };

    uint64_t phys = QEMU_CONNECT_HYPERCALL_PHYS;
    for (int i = 0; i < 4; i++) {
        qc_hypercall_note_store(phys + (uint64_t)i * 4, words[i], 4);
    }

    qc_hypercall_state_t st;
    if (!qc_hypercall_take_event(&st)) {
        fprintf(stderr, "FAIL: no event\n");
        return 1;
    }
    if (st.last_cmd != QEMU_CONNECT_HC_EXIT || st.last_status != 7) {
        fprintf(stderr, "FAIL: cmd=%u status=%u\n", st.last_cmd, st.last_status);
        return 1;
    }
    if (strcmp(st.last_name, "EXIT") != 0) {
        fprintf(stderr, "FAIL: name=%s\n", st.last_name);
        return 1;
    }
    if (qc_hypercall_take_event(&st)) {
        fprintf(stderr, "FAIL: event should be cleared\n");
        return 1;
    }

    /* READY */
    words[1] = QEMU_CONNECT_HC_READY;
    words[2] = 0;
    for (int i = 0; i < 4; i++) {
        qc_hypercall_note_store(phys + (uint64_t)i * 4, words[i], 4);
    }
    if (!qc_hypercall_take_event(&st) || strcmp(st.last_name, "READY") != 0) {
        fprintf(stderr, "FAIL: READY\n");
        return 1;
    }

    printf("OK test_hypercall_unit: EXIT/READY\n");
    return 0;
}
