/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect — QEMU TCG plugin for agent interaction with guests.
 *
 * Load:
 *   qemu-system-x86_64 ... \
 *     -plugin ./build/libqemu-connect.so,socket=/tmp/qemu-connect.sock
 *
 * Optional args:
 *   socket=PATH           control Unix socket (default /tmp/qemu-connect.sock)
 *   socket_thread=on|off  dedicated poll thread (default on)
 *   vga=on|off            instrument stores for 0xB8000 scrape (default on)
 */
#include <qemu-plugin.h>

#include "qemu-connect.h"
#include "mem.h"
#include "server.h"
#include "vga.h"

#include <stdio.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static qemu_plugin_id_t g_id;
static qc_vga_state_t g_vga;
static qc_server_t *g_server;
static char g_socket_path[256];
static bool g_socket_thread = true;
static bool g_vga_enabled = true;

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
        }
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;

    /* Fallback path when no dedicated socket thread. */
    if (!g_socket_thread && g_server) {
        qc_server_poll(g_server);
    }

    /* Instrument stores for VGA shadow (every TB, every insn). */
    if (g_vga_enabled) {
        qc_mem_instrument_tb(tb, &g_vga);
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
    qc_vga_destroy(&g_vga);
    qemu_plugin_outs("qemu-connect: unloaded\n");
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                          const qemu_info_t *info, int argc,
                                          char **argv)
{
    g_id = id;
    parse_args(argc, argv);
    qc_vga_init(&g_vga);

    g_server = qc_server_start(g_socket_path, &g_vga, g_socket_thread);
    if (!g_server) {
        qemu_plugin_outs("qemu-connect: failed to start control socket\n");
        qc_vga_destroy(&g_vga);
        return 1;
    }

    char msg[512];
    snprintf(msg, sizeof(msg),
             "qemu-connect: installed (target=%s system=%s socket=%s "
             "thread=%s vga=%s proto=%d.%d)\n",
             info && info->target_name ? info->target_name : "?",
             info && info->system_emulation ? "yes" : "no",
             qc_server_path(g_server),
             qc_server_uses_thread(g_server) ? "on" : "off",
             g_vga_enabled ? "on" : "off",
             QEMU_CONNECT_PROTO_MAJOR, QEMU_CONNECT_PROTO_MINOR);
    qemu_plugin_outs(msg);

    /*
     * Need TB translate when:
     *  - vga=on (register mem callbacks), or
     *  - socket_thread=off (drive server poll)
     */
    if (g_vga_enabled || !g_socket_thread) {
        qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    }
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
