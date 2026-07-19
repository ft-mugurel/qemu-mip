/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Path resolution for installed + dev layouts.
 *
 * Guest ISO/disk priority:
 *   1. QEMU_CONNECT_ISO / QEMU_CONNECT_DISK (process env)
 *   2. QEMU_CONNECT_GUEST from env
 *   3. QEMU_CONNECT_GUEST from $ROOT/.qemu-connect.local (or ~/.config/...)
 *   4. QEMU_CONNECT_ROOT/test/guest/build/...  (bundled clone fallback)
 *   5. ./test/guest/build/... from cwd
 *
 * Process environment always wins over .qemu-connect.local.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "paths.h"

#include <ctype.h>
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
static char g_guest[PATH_MAX];
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

/* Load KEY=VALUE lines; only setenv when the var is currently unset. */
static void load_env_file(const char *path)
{
    FILE *f;
    char line[PATH_MAX + 128];

    if (!path || !path[0] || !is_file(path)) {
        return;
    }
    f = fopen(path, "r");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char *eq;
        char *key;
        char *val;
        char *end;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '#' || *p == '\n' || *p == '\0') {
            continue;
        }
        if (strncmp(p, "export ", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }
        eq = strchr(p, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        key = p;
        val = eq + 1;
        /* trim key trailing space */
        end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        /* strip trailing newline/CR on value */
        end = val + strlen(val);
        while (end > val && (end[-1] == '\n' || end[-1] == '\r')) {
            *--end = '\0';
        }
        /* strip optional quotes around value */
        if ((val[0] == '"' || val[0] == '\'') && end > val + 1 &&
            end[-1] == val[0]) {
            val++;
            *--end = '\0';
        }
        if (!key[0]) {
            continue;
        }
        /* only known keys, and only if unset */
        if (strcmp(key, "QEMU_CONNECT_ROOT") != 0 &&
            strcmp(key, "QEMU_CONNECT_HOME") != 0 &&
            strcmp(key, "QEMU_CONNECT_GUEST") != 0 &&
            strcmp(key, "QEMU_CONNECT_ISO") != 0 &&
            strcmp(key, "QEMU_CONNECT_DISK") != 0 &&
            strcmp(key, "QEMU_CONNECT_PLUGIN") != 0 &&
            strcmp(key, "QEMU_CONNECT_CLI") != 0 &&
            strcmp(key, "QEMU_CONNECT_PROMPT") != 0) {
            continue;
        }
        if (!getenv(key) || !getenv(key)[0]) {
            setenv(key, val, 0);
        }
    }
    fclose(f);
}

static void load_local_configs(const char *root)
{
    char path[PATH_MAX];
    const char *home = getenv("HOME");

    if (root && root[0]) {
        snprintf(path, sizeof(path), "%s/.qemu-connect.local", root);
        load_env_file(path);
    }
    if (home && home[0]) {
        snprintf(path, sizeof(path), "%s/.config/qemu-connect/env", home);
        load_env_file(path);
    }
}

static void init_paths(void)
{
    if (g_init) {
        return;
    }
    g_init = 1;
    resolve_exe_dir();

    /* First pass: resolve tool root from env/cwd only (before local file). */
    {
        const char *env_root = getenv("QEMU_CONNECT_ROOT");
        const char *env_home = getenv("QEMU_CONNECT_HOME");

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
    }

    /*
     * Load project-local defaults (e.g. QEMU_CONNECT_GUEST=.../guest).
     * Does not override vars already set in the process environment.
     */
    load_local_configs(g_root);

    /* Re-read after local file may have set ROOT / GUEST / PLUGIN / ISO. */
    {
        const char *env_root = getenv("QEMU_CONNECT_ROOT");
        if (env_root && is_dir(env_root)) {
            set_path(g_root, sizeof(g_root), env_root);
        }
    }

    const char *env_plugin = getenv("QEMU_CONNECT_PLUGIN");
    const char *env_guest = getenv("QEMU_CONNECT_GUEST");
    const char *env_iso = getenv("QEMU_CONNECT_ISO");
    const char *env_disk = getenv("QEMU_CONNECT_DISK");

    /* guest tree */
    g_guest[0] = '\0';
    if (env_guest && is_dir(env_guest)) {
        set_path(g_guest, sizeof(g_guest), env_guest);
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
    } else if (g_guest[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/build/kernel.iso", g_guest);
        set_path(g_iso, sizeof(g_iso), p);
    } else {
        char iso1[PATH_MAX];
        snprintf(iso1, sizeof(iso1), "%s/test/guest/build/kernel.iso", g_root);
        if (is_file(iso1)) {
            set_path(g_iso, sizeof(g_iso), iso1);
        } else if (is_file("test/guest/build/kernel.iso")) {
            snprintf(g_iso, sizeof(g_iso), "test/guest/build/kernel.iso");
        } else {
            set_path(g_iso, sizeof(g_iso), iso1);
        }
    }

    /* disk */
    if (env_disk && env_disk[0]) {
        set_path(g_disk, sizeof(g_disk), env_disk);
    } else if (g_guest[0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/build/disk.img", g_guest);
        set_path(g_disk, sizeof(g_disk), p);
    } else {
        char disk1[PATH_MAX];
        snprintf(disk1, sizeof(disk1), "%s/test/guest/build/disk.img", g_root);
        if (is_file(disk1)) {
            set_path(g_disk, sizeof(g_disk), disk1);
        } else if (is_file("test/guest/build/disk.img")) {
            snprintf(g_disk, sizeof(g_disk), "test/guest/build/disk.img");
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
    return "Set QEMU_CONNECT_GUEST=/path/to/your/kernel, or put it in "
           "$QEMU_CONNECT_ROOT/.qemu-connect.local. Optional: "
           "QEMU_CONNECT_ISO / QEMU_CONNECT_DISK.";
}
