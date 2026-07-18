/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mem.h"

#include "hypercall.h"
#include "qemu-connect.h"

#include <stdatomic.h>
#include <stdio.h>

static atomic_uint_fast64_t g_mem_cb_fired;
static atomic_uint_fast64_t g_mem_cb_vga_hit;
static atomic_uint_fast64_t g_hwaddr_null;

static atomic_uint_fast64_t g_discon_exc;
static atomic_uint_fast64_t g_discon_irq;
static atomic_uint_fast64_t g_discon_host;
static atomic_uint_fast64_t g_discon_total;

static bool g_hypercall_on = true;

void qc_discon_init(void)
{
    atomic_store(&g_discon_exc, 0);
    atomic_store(&g_discon_irq, 0);
    atomic_store(&g_discon_host, 0);
    atomic_store(&g_discon_total, 0);
}

void qc_discon_on_event(int type_flags)
{
    atomic_fetch_add_explicit(&g_discon_total, 1, memory_order_relaxed);
    /* type is a single enum value in the callback */
    if (type_flags == QEMU_PLUGIN_DISCON_EXCEPTION ||
        (type_flags & QEMU_PLUGIN_DISCON_EXCEPTION)) {
        atomic_fetch_add_explicit(&g_discon_exc, 1, memory_order_relaxed);
    }
    if (type_flags == QEMU_PLUGIN_DISCON_INTERRUPT ||
        (type_flags & QEMU_PLUGIN_DISCON_INTERRUPT)) {
        atomic_fetch_add_explicit(&g_discon_irq, 1, memory_order_relaxed);
    }
    if (type_flags == QEMU_PLUGIN_DISCON_HOSTCALL ||
        (type_flags & QEMU_PLUGIN_DISCON_HOSTCALL)) {
        atomic_fetch_add_explicit(&g_discon_host, 1, memory_order_relaxed);
    }
}

uint64_t qc_discon_exception(void)
{
    return atomic_load_explicit(&g_discon_exc, memory_order_relaxed);
}
uint64_t qc_discon_interrupt(void)
{
    return atomic_load_explicit(&g_discon_irq, memory_order_relaxed);
}
uint64_t qc_discon_hostcall(void)
{
    return atomic_load_explicit(&g_discon_host, memory_order_relaxed);
}
uint64_t qc_discon_total(void)
{
    return atomic_load_explicit(&g_discon_total, memory_order_relaxed);
}

static uint64_t mem_value_as_u64(qemu_plugin_mem_value v)
{
    switch (v.type) {
    case QEMU_PLUGIN_MEM_VALUE_U8:
        return v.data.u8;
    case QEMU_PLUGIN_MEM_VALUE_U16:
        return v.data.u16;
    case QEMU_PLUGIN_MEM_VALUE_U32:
        return v.data.u32;
    case QEMU_PLUGIN_MEM_VALUE_U64:
        return v.data.u64;
    case QEMU_PLUGIN_MEM_VALUE_U128:
        return v.data.u128.low;
    default:
        return 0;
    }
}

typedef struct {
    qc_vga_state_t *vga;
    bool hypercall;
} mem_cb_ctx_t;

/* We pass vga as userdata; hypercall flag is process-global for simplicity. */

static void store_mem_cb(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                         uint64_t vaddr, void *userdata)
{
    (void)vcpu_index;
    qc_vga_state_t *vga = userdata;

    atomic_fetch_add_explicit(&g_mem_cb_fired, 1, memory_order_relaxed);

    if (!qemu_plugin_mem_is_store(info)) {
        return;
    }

    struct qemu_plugin_hwaddr *hw = qemu_plugin_get_hwaddr(info, vaddr);
    if (!hw) {
        uint64_t n = atomic_fetch_add_explicit(&g_hwaddr_null, 1,
                                               memory_order_relaxed);
        if (n == 0 || (n % 100000) == 0) {
            qemu_plugin_outs(
                "qemu-connect: mem_cb get_hwaddr NULL (rate-limited)\n");
        }
        return;
    }

    uint64_t phys = qemu_plugin_hwaddr_phys_addr(hw);
    qemu_plugin_mem_value val = qemu_plugin_mem_get_value(info);
    unsigned size = 1u << qemu_plugin_mem_size_shift(info);
    uint64_t raw = mem_value_as_u64(val);

    if (g_hypercall_on) {
        qc_hypercall_note_store(phys, raw, size);
    }

    if (vga && phys >= QEMU_CONNECT_VGA_TEXT_PHYS &&
        phys < QEMU_CONNECT_VGA_TEXT_PHYS + QEMU_CONNECT_VGA_BYTES) {
        atomic_fetch_add_explicit(&g_mem_cb_vga_hit, 1, memory_order_relaxed);
        qc_vga_note_store(vga, phys, raw, size);
    }
}

void qc_mem_instrument_tb(struct qemu_plugin_tb *tb, qc_vga_state_t *vga,
                          bool hypercall_on)
{
    g_hypercall_on = hypercall_on;
    if (!tb) {
        return;
    }
    /* Need instrumentation if either VGA or hypercall scrape is wanted. */
    if (!vga && !hypercall_on) {
        return;
    }
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        if (!insn) {
            continue;
        }
        /* Pass vga (may be NULL if only hypercall — still use dummy? pass vga always) */
        qemu_plugin_register_vcpu_mem_cb(insn, store_mem_cb, QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_W, vga);
    }
}

uint64_t qc_mem_cb_fired(void)
{
    return atomic_load_explicit(&g_mem_cb_fired, memory_order_relaxed);
}

uint64_t qc_mem_cb_vga_hit(void)
{
    return atomic_load_explicit(&g_mem_cb_vga_hit, memory_order_relaxed);
}
