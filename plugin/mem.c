/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Guest memory store instrumentation for VGA text scrape (0xB8000).
 *
 * On each TB translate we attach a write callback to every instruction.
 * At runtime, stores whose physical address falls in the VGA text window
 * update the host-side shadow buffer.
 */
#include "mem.h"

#include "qemu-connect.h"

#include <stdatomic.h>
#include <stdio.h>

static atomic_uint_fast64_t g_mem_cb_fired;
static atomic_uint_fast64_t g_mem_cb_vga_hit;
static atomic_uint_fast64_t g_hwaddr_null;

static uint64_t mem_value_as_u64(qemu_plugin_mem_value v)
{
    /* Host-endian payload per qemu-plugin.h; no extra byte-swap. */
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
        return v.data.u128.low; /* VGA cells are ≤ 8B */
    default:
        return 0;
    }
}

static void vga_mem_cb(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                       uint64_t vaddr, void *userdata)
{
    (void)vcpu_index;
    qc_vga_state_t *vga = userdata;

    atomic_fetch_add_explicit(&g_mem_cb_fired, 1, memory_order_relaxed);

    if (!vga || !qemu_plugin_mem_is_store(info)) {
        return;
    }

    struct qemu_plugin_hwaddr *hw = qemu_plugin_get_hwaddr(info, vaddr);
    if (!hw) {
        uint64_t n = atomic_fetch_add_explicit(&g_hwaddr_null, 1,
                                               memory_order_relaxed);
        /* Rate-limited diagnostic (every 100000 nulls). */
        if (n == 0 || (n % 100000) == 0) {
            qemu_plugin_outs(
                "qemu-connect: mem_cb get_hwaddr returned NULL (rate-limited)\n");
        }
        return;
    }

    /*
     * Do NOT skip is_io: PC std VGA text is often device memory, and
     * qemu_plugin_hwaddr_is_io() may be true for valid 0xB8000 stores.
     */
    uint64_t phys = qemu_plugin_hwaddr_phys_addr(hw);
    if (phys < QEMU_CONNECT_VGA_TEXT_PHYS ||
        phys >= QEMU_CONNECT_VGA_TEXT_PHYS + QEMU_CONNECT_VGA_BYTES) {
        return;
    }

    atomic_fetch_add_explicit(&g_mem_cb_vga_hit, 1, memory_order_relaxed);

    qemu_plugin_mem_value val = qemu_plugin_mem_get_value(info);
    unsigned size = 1u << qemu_plugin_mem_size_shift(info);
    uint64_t raw = mem_value_as_u64(val);

    /* munux: u16 cell = (attr << 8) | char — LE puts char in low byte. */
    qc_vga_note_store(vga, phys, raw, size);
}

void qc_mem_instrument_tb(struct qemu_plugin_tb *tb, qc_vga_state_t *vga)
{
    if (!tb || !vga) {
        return;
    }
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        if (!insn) {
            continue;
        }
        qemu_plugin_register_vcpu_mem_cb(insn, vga_mem_cb, QEMU_PLUGIN_CB_NO_REGS,
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
