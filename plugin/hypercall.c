/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Guest hypercall via stores to phys [0xFEE1DEAD, +16).
 * Inline ABI only (no guest pointers in v1).
 */
#include "hypercall.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t g_window[QEMU_CONNECT_HYPERCALL_SIZE];
static qc_hypercall_state_t g_st;

void qc_hypercall_init(void)
{
    pthread_mutex_lock(&g_lock);
    memset(g_window, 0, sizeof(g_window));
    memset(&g_st, 0, sizeof(g_st));
    pthread_mutex_unlock(&g_lock);
}

static const char *cmd_name(uint32_t cmd)
{
    switch (cmd) {
    case QEMU_CONNECT_HC_READY:
        return "READY";
    case QEMU_CONNECT_HC_EXIT:
        return "EXIT";
    case QEMU_CONNECT_HC_LOG:
        return "LOG";
    default:
        return "UNKNOWN";
    }
}

static void try_commit(void)
{
    qc_hypercall_msg_t msg;
    memcpy(&msg, g_window, sizeof(msg));
    if (msg.magic != QEMU_CONNECT_HYPERCALL_MAGIC) {
        return;
    }
    g_st.count++;
    g_st.last_cmd = msg.cmd;
    g_st.last_status = msg.status;
    g_st.have_event = true;
    snprintf(g_st.last_name, sizeof(g_st.last_name), "%s", cmd_name(msg.cmd));
}

void qc_hypercall_note_store(uint64_t phys, uint64_t value, unsigned size)
{
    if (phys + size <= QEMU_CONNECT_HYPERCALL_PHYS ||
        phys >= QEMU_CONNECT_HYPERCALL_PHYS + QEMU_CONNECT_HYPERCALL_SIZE) {
        return;
    }
    if (size == 0 || size > 8) {
        return;
    }

    pthread_mutex_lock(&g_lock);
    for (unsigned i = 0; i < size; i++) {
        uint64_t a = phys + i;
        if (a < QEMU_CONNECT_HYPERCALL_PHYS ||
            a >= QEMU_CONNECT_HYPERCALL_PHYS + QEMU_CONNECT_HYPERCALL_SIZE) {
            continue;
        }
        size_t off = (size_t)(a - QEMU_CONNECT_HYPERCALL_PHYS);
        g_window[off] = (uint8_t)((value >> (8 * i)) & 0xff);
    }
    try_commit();
    pthread_mutex_unlock(&g_lock);
}

void qc_hypercall_snapshot(qc_hypercall_state_t *out)
{
    if (!out) {
        return;
    }
    pthread_mutex_lock(&g_lock);
    *out = g_st;
    pthread_mutex_unlock(&g_lock);
}

bool qc_hypercall_take_event(qc_hypercall_state_t *out)
{
    if (!out) {
        return false;
    }
    pthread_mutex_lock(&g_lock);
    if (!g_st.have_event) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    *out = g_st;
    g_st.have_event = false;
    pthread_mutex_unlock(&g_lock);
    return true;
}
