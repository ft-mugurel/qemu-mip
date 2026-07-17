/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect — QEMU TCG plugin for agent interaction with guests.
 *
 * Load:
 *   qemu-system-x86_64 ... \
 *     -plugin ./build/libqemu-connect.so,socket=/tmp/qemu-connect.sock
 */
#include <qemu-plugin.h>

#include "qemu-connect.h"
#include "server.h"
#include "vga.h"

#include <stdio.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static qemu_plugin_id_t g_id;
static qc_vga_state_t g_vga;
static qc_server_t *g_server;
static char g_socket_path[256];

static void parse_args(int argc, char **argv)
{
    snprintf(g_socket_path, sizeof(g_socket_path), "%s",
             QEMU_CONNECT_DEFAULT_SOCK);
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!a) {
            continue;
        }
        if (strncmp(a, "socket=", 7) == 0) {
            snprintf(g_socket_path, sizeof(g_socket_path), "%s", a + 7);
        }
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    (void)tb;
    /*
     * v0.1: poll the control socket on every TB translation.
     * Later: mem callbacks on VGA stores + dedicated timer/idle hooks.
     */
    if (g_server) {
        qc_server_poll(g_server);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *userdata)
{
    (void)id;
    (void)userdata;
    if (g_server) {
        qc_server_stop(g_server);
        g_server = NULL;
    }
    qemu_plugin_outs("qemu-connect: unloaded\n");
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                          const qemu_info_t *info, int argc,
                                          char **argv)
{
    g_id = id;
    parse_args(argc, argv);
    qc_vga_init(&g_vga);

    g_server = qc_server_start(g_socket_path, &g_vga);
    if (!g_server) {
        qemu_plugin_outs("qemu-connect: failed to start control socket\n");
        return 1;
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
             "qemu-connect: installed (target=%s system=%s socket=%s proto=%d.%d)\n",
             info && info->target_name ? info->target_name : "?",
             info && info->system_emulation ? "yes" : "no",
             qc_server_path(g_server), QEMU_CONNECT_PROTO_MAJOR,
             QEMU_CONNECT_PROTO_MINOR);
    qemu_plugin_outs(msg);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
