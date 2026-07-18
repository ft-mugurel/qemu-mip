/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Single-slot work queue: server thread submits, vCPU context drains.
 * Used for operations that require qemu_plugin_read_memory_hwaddr().
 */
#include "queue.h"

#include "qemu-connect.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <qemu-plugin.h>
#include <glib.h>

struct qc_queue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int default_timeout_ms;

    bool pending;
    bool done;
    bool cancelled;

    qc_queue_status_t status;
    int hw_code;
};

qc_queue_t *qc_queue_create(int default_timeout_ms)
{
    qc_queue_t *q = calloc(1, sizeof(*q));
    if (!q) {
        return NULL;
    }
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->default_timeout_ms = default_timeout_ms > 0
                                ? default_timeout_ms
                                : QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT;
    return q;
}

void qc_queue_destroy(qc_queue_t *q)
{
    if (!q) {
        return;
    }
    pthread_mutex_lock(&q->lock);
    q->cancelled = true;
    q->pending = false;
    q->done = true;
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
    pthread_cond_destroy(&q->cond);
    pthread_mutex_destroy(&q->lock);
    free(q);
}

void qc_queue_set_timeout_ms(qc_queue_t *q, int timeout_ms)
{
    if (!q) {
        return;
    }
    pthread_mutex_lock(&q->lock);
    q->default_timeout_ms = timeout_ms > 0
                                ? timeout_ms
                                : QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT;
    pthread_mutex_unlock(&q->lock);
}

int qc_queue_timeout_ms(const qc_queue_t *q)
{
    return q ? q->default_timeout_ms : QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT;
}

static void timespec_add_ms(struct timespec *ts, int ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

qc_queue_result_t qc_queue_request_vga_refresh(qc_queue_t *q, qc_vga_state_t *vga,
                                               int timeout_ms_override)
{
    qc_queue_result_t r = { .status = QC_QUEUE_DISABLED, .hw_code = 0 };
    if (!q || !vga) {
        return r;
    }

    int timeout_ms = timeout_ms_override > 0 ? timeout_ms_override
                                             : q->default_timeout_ms;

    pthread_mutex_lock(&q->lock);

    /* Wait for previous job to finish (should be rare with single client). */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    timespec_add_ms(&deadline, timeout_ms);

    while (q->pending && !q->done) {
        int e = pthread_cond_timedwait(&q->cond, &q->lock, &deadline);
        if (e == ETIMEDOUT) {
            r.status = QC_QUEUE_BUSY;
            pthread_mutex_unlock(&q->lock);
            return r;
        }
    }

    q->pending = true;
    q->done = false;
    q->cancelled = false;
    q->status = QC_QUEUE_TIMEOUT;
    q->hw_code = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    timespec_add_ms(&deadline, timeout_ms);

    while (!q->done) {
        int e = pthread_cond_timedwait(&q->cond, &q->lock, &deadline);
        if (e == ETIMEDOUT) {
            q->cancelled = true;
            q->pending = false;
            q->done = true;
            r.status = QC_QUEUE_TIMEOUT;
            pthread_cond_broadcast(&q->cond);
            pthread_mutex_unlock(&q->lock);
            return r;
        }
    }

    r.status = q->status;
    r.hw_code = q->hw_code;
    q->pending = false;
    pthread_mutex_unlock(&q->lock);
    return r;
}

void qc_queue_drain(qc_queue_t *q, qc_vga_state_t *vga)
{
    if (!q || !vga) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    if (!q->pending || q->done || q->cancelled) {
        pthread_mutex_unlock(&q->lock);
        return;
    }
    /* Claim the job for this vCPU. */
    q->pending = false;
    pthread_mutex_unlock(&q->lock);

    GByteArray *buf = g_byte_array_sized_new(QEMU_CONNECT_VGA_BYTES);
    enum qemu_plugin_hwaddr_operation_result code =
        qemu_plugin_read_memory_hwaddr(QEMU_CONNECT_VGA_TEXT_PHYS, buf,
                                       QEMU_CONNECT_VGA_BYTES);

    pthread_mutex_lock(&q->lock);
    if (q->cancelled) {
        /* Submitter already timed out; drop result. */
        g_byte_array_unref(buf);
        q->done = true;
        pthread_cond_broadcast(&q->cond);
        pthread_mutex_unlock(&q->lock);
        return;
    }

    q->hw_code = (int)code;
    if (code == QEMU_PLUGIN_HWADDR_OPERATION_OK && buf->len >= 2) {
        qc_vga_load_cells(vga, buf->data, buf->len);
        q->status = QC_QUEUE_OK;
    } else {
        q->status = QC_QUEUE_HW_FAIL;
    }
    q->done = true;
    g_byte_array_unref(buf);
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
}
