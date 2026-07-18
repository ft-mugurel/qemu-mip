/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <qemu-plugin.h>

#include "qemu-connect.h"
#include "hypercall.h"
#include "mem.h"
#include "protocol.h"
#include "queue.h"
#include "server.h"
#include "vga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static qc_vga_state_t g_vga;
static qc_queue_t *g_queue;
static qc_proto_ctx_t g_ctx;
static qc_server_t *g_server;
static char g_socket_path[256];
static bool g_socket_thread = true;
static bool g_vga_enabled = true;
static bool g_vga_refresh = true;
static bool g_hypercall = true;
static int g_queue_timeout_ms = QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT;

static bool parse_bool_arg(const char *val, bool default_val)
{
    if (!val || !val[0]) {
        return default_val;
    }
    if (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 ||
        strcmp(val, "true") == 0 || strcmp(val, "yes") == 0) {
        return true;
    }
    if (strcmp(val, "off") == 0 || strcmp(val, "0") == 0 ||
        strcmp(val, "false") == 0 || strcmp(val, "no") == 0) {
        return false;
    }
    return default_val;
}

static void parse_args(int argc, char **argv)
{
    snprintf(g_socket_path, sizeof(g_socket_path), "%s",
             QEMU_CONNECT_DEFAULT_SOCK);
    g_socket_thread = true;
    g_vga_enabled = true;
    g_vga_refresh = true;
    g_hypercall = true;
    g_queue_timeout_ms = QEMU_CONNECT_QUEUE_TIMEOUT_MS_DEFAULT;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!a) {
            continue;
        }
        if (strncmp(a, "socket=", 7) == 0) {
            snprintf(g_socket_path, sizeof(g_socket_path), "%s", a + 7);
        } else if (strncmp(a, "socket_thread=", 14) == 0) {
            g_socket_thread = parse_bool_arg(a + 14, true);
        } else if (strncmp(a, "vga=", 4) == 0) {
            g_vga_enabled = parse_bool_arg(a + 4, true);
        } else if (strncmp(a, "vga_refresh=", 12) == 0) {
            g_vga_refresh = parse_bool_arg(a + 12, true);
        } else if (strncmp(a, "hypercall=", 10) == 0) {
            g_hypercall = parse_bool_arg(a + 10, true);
        } else if (strncmp(a, "vcpu_queue_timeout_ms=", 22) == 0) {
            int v = atoi(a + 22);
            if (v > 0) {
                g_queue_timeout_ms = v;
            }
        }
    }
}

static void drain_queue(void)
{
    if (g_queue) {
        qc_queue_drain(g_queue, &g_vga);
    }
}

static void vcpu_tb_exec(unsigned int vcpu_index, void *userdata)
{
    (void)vcpu_index;
    (void)userdata;
    drain_queue();
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    drain_queue();

    if (!g_socket_thread && g_server) {
        qc_server_poll(g_server);
    }

    if (g_vga_enabled || g_hypercall) {
        qc_mem_instrument_tb(tb, g_vga_enabled ? &g_vga : NULL, g_hypercall);
    }

    if (g_queue) {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS, NULL);
    }
}

static void vcpu_idle(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    (void)id;
    (void)vcpu_index;
    drain_queue();
}

static void vcpu_discon(qemu_plugin_id_t id, unsigned int vcpu_index,
                        enum qemu_plugin_discon_type type, uint64_t from_pc,
                        uint64_t to_pc)
{
    (void)id;
    (void)vcpu_index;
    (void)from_pc;
    (void)to_pc;
    qc_discon_on_event((int)type);
}

static void plugin_exit(qemu_plugin_id_t id, void *userdata)
{
    (void)id;
    (void)userdata;
    if (g_server) {
        qc_server_stop(g_server);
        g_server = NULL;
    }
    if (g_queue) {
        qc_queue_destroy(g_queue);
        g_queue = NULL;
    }
    qc_vga_destroy(&g_vga);
    qemu_plugin_outs("qemu-connect: unloaded\n");
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                          const qemu_info_t *info, int argc,
                                          char **argv)
{
    parse_args(argc, argv);
    qc_vga_init(&g_vga);
    qc_hypercall_init();
    qc_discon_init();

    g_queue = qc_queue_create(g_queue_timeout_ms);
    if (!g_queue) {
        qemu_plugin_outs("qemu-connect: failed to create queue\n");
        qc_vga_destroy(&g_vga);
        return 1;
    }

    g_ctx.vga = &g_vga;
    g_ctx.queue = g_queue;
    g_ctx.vga_refresh_enabled = g_vga_refresh;
    g_ctx.socket_thread = g_socket_thread;
    g_ctx.vga_enabled = g_vga_enabled;
    g_ctx.hypercall_enabled = g_hypercall;

    g_server = qc_server_start(g_socket_path, &g_ctx, g_socket_thread);
    if (!g_server) {
        qemu_plugin_outs("qemu-connect: failed to start control socket\n");
        qc_queue_destroy(g_queue);
        g_queue = NULL;
        qc_vga_destroy(&g_vga);
        return 1;
    }

    char msg[768];
    snprintf(msg, sizeof(msg),
             "qemu-connect: installed (target=%s system=%s socket=%s "
             "thread=%s vga=%s refresh=%s hypercall=%s qtimeout=%dms "
             "proto=%d.%d)\n",
             info && info->target_name ? info->target_name : "?",
             info && info->system_emulation ? "yes" : "no",
             qc_server_path(g_server),
             qc_server_uses_thread(g_server) ? "on" : "off",
             g_vga_enabled ? "on" : "off", g_vga_refresh ? "on" : "off",
             g_hypercall ? "on" : "off", g_queue_timeout_ms,
             QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR);
    qemu_plugin_outs(msg);

    /* Always instrument translate so queue can drain on exec. */
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_vcpu_idle_cb(id, vcpu_idle);
    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL, vcpu_discon);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
