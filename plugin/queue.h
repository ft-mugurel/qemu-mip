/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_CONNECT_QUEUE_H
#define QEMU_CONNECT_QUEUE_H

#include <stdbool.h>

#include "vga.h"

typedef struct qc_queue qc_queue_t;

typedef enum {
    QC_QUEUE_OK = 0,
    QC_QUEUE_TIMEOUT = 1,
    QC_QUEUE_HW_FAIL = 2,
    QC_QUEUE_BUSY = 3,
    QC_QUEUE_DISABLED = 4,
} qc_queue_status_t;

typedef struct {
    qc_queue_status_t status;
    int hw_code; /* qemu_plugin_hwaddr_operation_result when HW_FAIL */
} qc_queue_result_t;

qc_queue_t *qc_queue_create(int default_timeout_ms);
void qc_queue_destroy(qc_queue_t *q);

void qc_queue_set_timeout_ms(qc_queue_t *q, int timeout_ms);
int qc_queue_timeout_ms(const qc_queue_t *q);

/*
 * Server-thread API: request VGA phys read + rebuild shadow.
 * Blocks until vCPU drain completes or timeout (never hangs forever).
 */
qc_queue_result_t qc_queue_request_vga_refresh(qc_queue_t *q, qc_vga_state_t *vga,
                                               int timeout_ms_override);

/*
 * vCPU-context API: process at most one pending job (safe after permanent hlt
 * only if no job is pending — submitter times out instead).
 */
void qc_queue_drain(qc_queue_t *q, qc_vga_state_t *vga);

#endif
