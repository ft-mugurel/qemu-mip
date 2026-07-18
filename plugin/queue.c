/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "queue.h"

#include "qemu-connect.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <qemu-plugin.h>

typedef enum {
    QC_JOB_NONE = 0,
    QC_JOB_VGA_REFRESH,
    QC_JOB_MEM_READ,
} qc_job_kind;

struct qc_queue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int default_timeout_ms;

    bool pending;
    bool done;
    bool cancelled;

    qc_job_kind kind;
    qc_queue_status_t status;
    int hw_code;

    uint64_t phys;
    size_t len;
    uint8_t *out; /* caller-owned for MEM_READ */
    size_t data_len;
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

static qc_queue_result_t submit_and_wait(qc_queue_t *q, int timeout_ms)
{
    qc_queue_result_t r = { .status = QC_QUEUE_DISABLED, .hw_code = 0,
                            .data_len = 0 };

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    timespec_add_ms(&deadline, timeout_ms);

    while (q->pending && !q->done) {
        int e = pthread_cond_timedwait(&q->cond, &q->lock, &deadline);
        if (e == ETIMEDOUT) {
            r.status = QC_QUEUE_BUSY;
            return r;
        }
    }

    q->pending = true;
    q->done = false;
    q->cancelled = false;
    q->status = QC_QUEUE_TIMEOUT;
    q->hw_code = 0;
    q->data_len = 0;

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
            return r;
        }
    }

    r.status = q->status;
    r.hw_code = q->hw_code;
    r.data_len = q->data_len;
    q->pending = false;
    q->kind = QC_JOB_NONE;
    return r;
}

qc_queue_result_t qc_queue_request_vga_refresh(qc_queue_t *q, qc_vga_state_t *vga,
                                               int timeout_ms_override)
{
    qc_queue_result_t r = { .status = QC_QUEUE_DISABLED };
    if (!q || !vga) {
        return r;
    }
    int timeout_ms = timeout_ms_override > 0 ? timeout_ms_override
                                             : q->default_timeout_ms;
    pthread_mutex_lock(&q->lock);
    q->kind = QC_JOB_VGA_REFRESH;
    q->out = NULL;
    q->phys = QEMU_CONNECT_VGA_TEXT_PHYS;
    q->len = QEMU_CONNECT_VGA_BYTES;
    r = submit_and_wait(q, timeout_ms);
    pthread_mutex_unlock(&q->lock);
    return r;
}

qc_queue_result_t qc_queue_request_mem_read(qc_queue_t *q, uint64_t phys,
                                            size_t len, uint8_t *out,
                                            int timeout_ms_override)
{
    qc_queue_result_t r = { .status = QC_QUEUE_BAD_ARG };
    if (!q || !out || len == 0 || len > QEMU_CONNECT_MEM_READ_MAX) {
        return r;
    }
    int timeout_ms = timeout_ms_override > 0 ? timeout_ms_override
                                             : q->default_timeout_ms;
    pthread_mutex_lock(&q->lock);
    q->kind = QC_JOB_MEM_READ;
    q->phys = phys;
    q->len = len;
    q->out = out;
    r = submit_and_wait(q, timeout_ms);
    pthread_mutex_unlock(&q->lock);
    return r;
}

void qc_queue_drain(qc_queue_t *q, qc_vga_state_t *vga)
{
    if (!q) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    if (!q->pending || q->done || q->cancelled) {
        pthread_mutex_unlock(&q->lock);
        return;
    }
    qc_job_kind kind = q->kind;
    uint64_t phys = q->phys;
    size_t len = q->len;
    uint8_t *out = q->out;
    q->pending = false;
    pthread_mutex_unlock(&q->lock);

    GByteArray *buf = g_byte_array_sized_new(len ? len : 1);
    enum qemu_plugin_hwaddr_operation_result code =
        qemu_plugin_read_memory_hwaddr(phys, buf, len);

    pthread_mutex_lock(&q->lock);
    if (q->cancelled) {
        g_byte_array_unref(buf);
        q->done = true;
        pthread_cond_broadcast(&q->cond);
        pthread_mutex_unlock(&q->lock);
        return;
    }

    q->hw_code = (int)code;
    if (code == QEMU_PLUGIN_HWADDR_OPERATION_OK && buf->data && buf->len > 0) {
        if (kind == QC_JOB_VGA_REFRESH && vga) {
            qc_vga_load_cells(vga, buf->data, buf->len);
            q->status = QC_QUEUE_OK;
            q->data_len = buf->len;
        } else if (kind == QC_JOB_MEM_READ && out) {
            size_t n = buf->len < len ? buf->len : len;
            memcpy(out, buf->data, n);
            q->data_len = n;
            q->status = QC_QUEUE_OK;
        } else {
            q->status = QC_QUEUE_HW_FAIL;
        }
    } else {
        q->status = QC_QUEUE_HW_FAIL;
    }
    q->done = true;
    g_byte_array_unref(buf);
    pthread_cond_broadcast(&q->cond);
    pthread_mutex_unlock(&q->lock);
}
