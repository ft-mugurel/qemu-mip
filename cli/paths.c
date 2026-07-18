/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "paths.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_exe_dir[PATH_MAX];
static char g_plugin[PATH_MAX];
static char g_iso[PATH_MAX];
static char g_disk[PATH_MAX];
static char g_root[PATH_MAX];
static int g_init;

static int is_file(const char *p)
{
    struct stat st;
    return p && p[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_dir(const char *p)
{
    struct stat st;
    return p && p[0] && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void dirname_into(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash && slash != out) {
        *slash = '\0';
    } else if (slash == out) {
        out[1] = '\0';
    } else {
        snprintf(out, n, ".");
    }
}

static void resolve_exe_dir(void)
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        dirname_into(buf, g_exe_dir, sizeof(g_exe_dir));
        return;
    }
    snprintf(g_exe_dir, sizeof(g_exe_dir), ".");
}

static void init_paths(void)
{
    if (g_init) {
        return;
    }
    g_init = 1;
    resolve_exe_dir();

    const char *env_root = getenv("QEMU_CONNECT_ROOT");
    const char *env_home = getenv("QEMU_CONNECT_HOME");
    const char *env_plugin = getenv("QEMU_CONNECT_PLUGIN");

    /* install root / repo root candidates */
    if (env_root && is_dir(env_root)) {
        snprintf(g_root, sizeof(g_root), "%s", env_root);
    } else if (env_home && is_dir(env_home)) {
        snprintf(g_root, sizeof(g_root), "%s", env_home);
    } else {
        /* PREFIX/bin/qemu-connect → PREFIX/share/qemu-connect */
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s/../share/qemu-connect", g_exe_dir);
        if (is_dir(cand)) {
            char real[PATH_MAX];
            if (realpath(cand, real)) {
                snprintf(g_root, sizeof(g_root), "%s", real);
            } else {
                snprintf(g_root, sizeof(g_root), "%s", cand);
            }
        } else {
            /* dev tree: cwd or parent of build/ */
            if (is_file("build/qemu-connect") || is_file("Makefile")) {
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd))) {
                    snprintf(g_root, sizeof(g_root), "%s", cwd);
                } else {
                    snprintf(g_root, sizeof(g_root), ".");
                }
            } else {
                snprintf(g_root, sizeof(g_root), "%s/..", g_exe_dir);
            }
        }
    }

    /* plugin */
    if (env_plugin && is_file(env_plugin)) {
        snprintf(g_plugin, sizeof(g_plugin), "%s", env_plugin);
    } else {
        char c1[PATH_MAX], c2[PATH_MAX], c3[PATH_MAX];
        snprintf(c1, sizeof(c1), "%s/../lib/qemu-connect/libqemu-connect.so",
                 g_exe_dir);
        snprintf(c2, sizeof(c2), "%s/libqemu-connect.so", g_root);
        snprintf(c3, sizeof(c3), "%s/build/libqemu-connect.so", g_root);
        if (is_file(c1)) {
            if (!realpath(c1, g_plugin)) {
                snprintf(g_plugin, sizeof(g_plugin), "%s", c1);
            }
        } else if (is_file(c3)) {
            snprintf(g_plugin, sizeof(g_plugin), "%s", c3);
        } else if (is_file(c2)) {
            snprintf(g_plugin, sizeof(g_plugin), "%s", c2);
        } else if (is_file("build/libqemu-connect.so")) {
            snprintf(g_plugin, sizeof(g_plugin), "build/libqemu-connect.so");
        } else {
            snprintf(g_plugin, sizeof(g_plugin), "%s/build/libqemu-connect.so",
                     g_root);
        }
    }

    /* munux artifacts: prefer workspace under QEMU_CONNECT_ROOT or cwd */
    char iso1[PATH_MAX], disk1[PATH_MAX];
    snprintf(iso1, sizeof(iso1), "%s/test/munux/build/kernel.iso", g_root);
    snprintf(disk1, sizeof(disk1), "%s/test/munux/build/disk.img", g_root);
    if (is_file(iso1)) {
        snprintf(g_iso, sizeof(g_iso), "%s", iso1);
    } else if (is_file("test/munux/build/kernel.iso")) {
        snprintf(g_iso, sizeof(g_iso), "test/munux/build/kernel.iso");
    } else {
        snprintf(g_iso, sizeof(g_iso), "%s", iso1);
    }
    if (is_file(disk1)) {
        snprintf(g_disk, sizeof(g_disk), "%s", disk1);
    } else if (is_file("test/munux/build/disk.img")) {
        snprintf(g_disk, sizeof(g_disk), "test/munux/build/disk.img");
    } else {
        snprintf(g_disk, sizeof(g_disk), "%s", disk1);
    }
}

const char *qc_install_root(void)
{
    init_paths();
    return g_root;
}

const char *qc_default_plugin(void)
{
    init_paths();
    return g_plugin;
}

const char *qc_default_iso(void)
{
    init_paths();
    return g_iso;
}

const char *qc_default_disk(void)
{
    init_paths();
    return g_disk;
}

const char *qc_default_cli_hint(void)
{
    return "Set QEMU_CONNECT_ROOT to your checkout, or run from the repo root.";
}
