/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Path resolution for installed + dev layouts.
 *
 * Guest ISO/disk priority:
 *   1. QEMU_CONNECT_ISO / QEMU_CONNECT_DISK
 *   2. QEMU_CONNECT_MUNUX/build/{kernel.iso,disk.img}  ← your real munux tree
 *   3. QEMU_CONNECT_ROOT/test/munux/build/...           ← old bundled clone
 *   4. ./test/munux/build/... from cwd
 */
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
static char g_munux[PATH_MAX];
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

static void set_path(char *dst, size_t n, const char *src)
{
    char real[PATH_MAX];
    if (realpath(src, real)) {
        snprintf(dst, n, "%s", real);
    } else {
        snprintf(dst, n, "%s", src);
    }
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
    const char *env_munux = getenv("QEMU_CONNECT_MUNUX");
    const char *env_iso = getenv("QEMU_CONNECT_ISO");
    const char *env_disk = getenv("QEMU_CONNECT_DISK");

    if (env_root && is_dir(env_root)) {
        set_path(g_root, sizeof(g_root), env_root);
    } else if (env_home && is_dir(env_home)) {
        set_path(g_root, sizeof(g_root), env_home);
    } else {
        char cand[PATH_MAX];
        snprintf(cand, sizeof(cand), "%s/../share/qemu-connect", g_exe_dir);
        if (is_dir(cand)) {
            set_path(g_root, sizeof(g_root), cand);
        } else if (is_file("build/qemu-connect") || is_file("Makefile")) {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd))) {
                set_path(g_root, sizeof(g_root), cwd);
            } else {
                snprintf(g_root, sizeof(g_root), ".");
            }
        } else {
            snprintf(g_root, sizeof(g_root), "%s/..", g_exe_dir);
        }
    }

    /* munux tree */
    g_munux[0] = '\0';
    if (env_munux && is_dir(env_munux)) {
        set_path(g_munux, sizeof(g_munux), env_munux);
    }

    /* plugin */
    if (env_plugin && is_file(env_plugin)) {
        set_path(g_plugin, sizeof(g_plugin), env_plugin);
    } else {
        char c1[PATH_MAX], c2[PATH_MAX], c3[PATH_MAX];
        snprintf(c1, sizeof(c1), "%s/../lib/qemu-connect/libqemu-connect.so",
                 g_exe_dir);
        snprintf(c2, sizeof(c2), "%s/libqemu-connect.so", g_root);
        snprintf(c3, sizeof(c3), "%s/build/libqemu-connect.so", g_root);
        if (is_file(c1)) {
            set_path(g_plugin, sizeof(g_plugin), c1);
        } else if (is_file(c3)) {
            set_path(g_plugin, sizeof(g_plugin), c3);
        } else if (is_file(c2)) {
            set_path(g_plugin, sizeof(g_plugin), c2);
        } else if (is_file("build/libqemu-connect.so")) {
            snprintf(g_plugin, sizeof(g_plugin), "build/libqemu-connect.so");
        } else {
            snprintf(g_plugin, sizeof(g_plugin), "%s/build/libqemu-connect.so",
                     g_root);
        }
    }

    /* ISO */
    if (env_iso && env_iso[0]) {
        set_path(g_iso, sizeof(g_iso), env_iso);
    } else if (g_munux[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/build/kernel.iso", g_munux);
        set_path(g_iso, sizeof(g_iso), p);
    } else {
        char iso1[PATH_MAX];
        snprintf(iso1, sizeof(iso1), "%s/test/munux/build/kernel.iso", g_root);
        if (is_file(iso1)) {
            set_path(g_iso, sizeof(g_iso), iso1);
        } else if (is_file("test/munux/build/kernel.iso")) {
            snprintf(g_iso, sizeof(g_iso), "test/munux/build/kernel.iso");
        } else {
            set_path(g_iso, sizeof(g_iso), iso1);
        }
    }

    /* disk */
    if (env_disk && env_disk[0]) {
        set_path(g_disk, sizeof(g_disk), env_disk);
    } else if (g_munux[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/build/disk.img", g_munux);
        set_path(g_disk, sizeof(g_disk), p);
    } else {
        char disk1[PATH_MAX];
        snprintf(disk1, sizeof(disk1), "%s/test/munux/build/disk.img", g_root);
        if (is_file(disk1)) {
            set_path(g_disk, sizeof(g_disk), disk1);
        } else if (is_file("test/munux/build/disk.img")) {
            snprintf(g_disk, sizeof(g_disk), "test/munux/build/disk.img");
        } else {
            set_path(g_disk, sizeof(g_disk), disk1);
        }
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
    return "Set QEMU_CONNECT_MUNUX=/path/to/your/munux (and QEMU_CONNECT_ROOT "
           "for the tool repo). Optional: QEMU_CONNECT_ISO / QEMU_CONNECT_DISK.";
}
