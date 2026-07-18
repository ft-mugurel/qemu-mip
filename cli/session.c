/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Persistent QEMU session for multi-command agent loops.
 *
 *   qemu-connect session start
 *   qemu-connect session cmd help
 *   qemu-connect session cmd ls
 *   qemu-connect session console
 *   qemu-connect session stop
 */
#define _POSIX_C_SOURCE 200809L
#include "session.h"

#include "qemu-connect.h"
#include "qmp.h"
#include "paths.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define QC_SESS_OK 0
#define QC_SESS_FAIL 1
#define QC_SESS_USAGE 2
#define QC_SESS_GONE 3
#define QC_SESS_CONNECT 4

typedef struct {
    char id[64];
    pid_t pid;
    char plugin_sock[256];
    char qmp_sock[256];
    char logpath[256];
    char iso[512];
    char disk[512];
} qc_session_t;

static void sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static void json_escape_print(FILE *f, const char *s)
{
    fputc('"', f);
    if (!s) {
        fputc('"', f);
        return;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", f);
            break;
        case '\\':
            fputs("\\\\", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (*p < 0x20) {
                fprintf(f, "\\u%04x", *p);
            } else {
                fputc(*p, f);
            }
            break;
        }
    }
    fputc('"', f);
}

static void print_result(bool ok, int exit_code, const char *session_id,
                         const char *op, const char *console,
                         const char *error, long long duration_ms)
{
    printf("{\"ok\":%s,\"exit_code\":%d", ok ? "true" : "false", exit_code);
    if (session_id) {
        printf(",\"session_id\":");
        json_escape_print(stdout, session_id);
    }
    if (op) {
        printf(",\"op\":");
        json_escape_print(stdout, op);
    }
    if (duration_ms >= 0) {
        printf(",\"duration_ms\":%lld", duration_ms);
    }
    if (error && error[0]) {
        printf(",\"error\":");
        json_escape_print(stdout, error);
    }
    if (console) {
        printf(",\"console\":");
        json_escape_print(stdout, console);
    }
    printf("}\n");
}

static const char *runtime_dir(void)
{
    const char *r = getenv("XDG_RUNTIME_DIR");
    if (r && r[0]) {
        return r;
    }
    r = getenv("TMPDIR");
    if (r && r[0]) {
        return r;
    }
    return "/tmp";
}

static void session_dir(char *out, size_t n)
{
    snprintf(out, n, "%s/qemu-connect-sessions", runtime_dir());
}

static void session_path(const char *id, char *out, size_t n)
{
    char dir[256];
    session_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/%s.json", dir, id);
}

static int connect_unix(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int plugin_request(const char *sock, const char *line, char *buf,
                          size_t buflen)
{
    int fd = connect_unix(sock);
    if (fd < 0) {
        return -1;
    }
    char req[4096];
    snprintf(req, sizeof(req), "%s\n", line);
    if (write(fd, req, strlen(req)) < 0) {
        close(fd);
        return -1;
    }
    size_t total = 0;
    while (total + 1 < buflen) {
        ssize_t n = read(fd, buf + total, buflen - 1 - total);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
        if (memchr(buf, '\n', total)) {
            break;
        }
    }
    close(fd);
    buf[total] = '\0';
    return 0;
}

static int extract_text(const char *json, char *dst, size_t dst_len)
{
    const char *key = "\"text\":\"";
    const char *p = strstr(json, key);
    if (!p || dst_len == 0) {
        return -1;
    }
    p += strlen(key);
    size_t o = 0;
    while (*p && o + 1 < dst_len) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':
                dst[o++] = '\n';
                break;
            case 'r':
                dst[o++] = '\r';
                break;
            case 't':
                dst[o++] = '\t';
                break;
            case '"':
                dst[o++] = '"';
                break;
            case '\\':
                dst[o++] = '\\';
                break;
            default:
                dst[o++] = *p;
                break;
            }
            p++;
            continue;
        }
        if (*p == '"') {
            break;
        }
        dst[o++] = *p++;
    }
    dst[o] = '\0';
    return 0;
}

static int get_console_text(const char *psock, char *out, size_t out_len)
{
    char json[QEMU_CONNECT_RESP_MAX];
    if (plugin_request(psock, "{\"cmd\":\"get_console\"}", json, sizeof(json)) !=
        0) {
        return -1;
    }
    return extract_text(json, out, out_len);
}

static int wait_expect(const char *psock, const char *needle, int timeout_ms)
{
    long long deadline = now_ms() + timeout_ms;
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    while (now_ms() < deadline) {
        if (get_console_text(psock, text, sizeof(text)) == 0 &&
            strstr(text, needle)) {
            return 0;
        }
        sleep_ms(100);
    }
    return -1;
}

static int pid_alive(pid_t pid)
{
    if (pid <= 0) {
        return 0;
    }
    return kill(pid, 0) == 0 || errno == EPERM;
}

static int save_session(const qc_session_t *s)
{
    char dir[256], path[320];
    session_dir(dir, sizeof(dir));
    mkdir(dir, 0700);
    session_path(s->id, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f,
            "{\n"
            "  \"id\": \"%s\",\n"
            "  \"pid\": %d,\n"
            "  \"plugin_sock\": \"%s\",\n"
            "  \"qmp_sock\": \"%s\",\n"
            "  \"log\": \"%s\",\n"
            "  \"iso\": \"%s\",\n"
            "  \"disk\": \"%s\"\n"
            "}\n",
            s->id, (int)s->pid, s->plugin_sock, s->qmp_sock, s->logpath, s->iso,
            s->disk);
    fclose(f);
    return 0;
}

/* Minimal JSON field extractors for our own state file (no full parser). */
static int json_get_string(const char *json, const char *key, char *out,
                           size_t out_len)
{
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\": \"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "\"%s\":\"", key);
        p = strstr(json, pat);
        if (!p) {
            return -1;
        }
        p += strlen(pat);
    } else {
        p += strlen(pat);
    }
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int json_get_int(const char *json, const char *key, long *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    while (*p == ' ') {
        p++;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return -1;
    }
    *out = v;
    return 0;
}

static int load_session(const char *id, qc_session_t *s)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->id, sizeof(s->id), "%s", id);
    char path[320];
    session_path(id, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    long pid = 0;
    if (json_get_int(buf, "pid", &pid) != 0) {
        return -1;
    }
    s->pid = (pid_t)pid;
    if (json_get_string(buf, "plugin_sock", s->plugin_sock,
                        sizeof(s->plugin_sock)) != 0 ||
        json_get_string(buf, "qmp_sock", s->qmp_sock, sizeof(s->qmp_sock)) !=
            0) {
        return -1;
    }
    json_get_string(buf, "log", s->logpath, sizeof(s->logpath));
    json_get_string(buf, "iso", s->iso, sizeof(s->iso));
    json_get_string(buf, "disk", s->disk, sizeof(s->disk));
    return 0;
}

static void remove_session_file(const char *id)
{
    char path[320];
    session_path(id, path, sizeof(path));
    unlink(path);
}

static void usage(void)
{
    fprintf(stderr,
            "Usage: qemu-connect session <subcommand> [options]\n"
            "\n"
            "  start [--id NAME] [--iso PATH] [--disk PATH] [--plugin PATH]\n"
            "        [--timeout MS] [--no-wait]\n"
            "      Boot QEMU once; wait for munux> (unless --no-wait).\n"
            "\n"
            "  cmd <shell words...> [--id NAME] [--timeout MS]\n"
            "      Type a shell command + Enter; wait for munux> again.\n"
            "\n"
            "  expect <text> [--id NAME] [--timeout MS]\n"
            "  console [--id NAME]     Print console JSON + text field\n"
            "  status [--id NAME]      Session + guest status\n"
            "  stop [--id NAME]        QMP quit + cleanup\n"
            "\n"
            "Default id: default\n"
            "Default munux paths: test/munux/build/kernel.iso + disk.img\n"
            "All subcommands print one JSON object on stdout.\n");
}

static int session_start(int argc, char **argv)
{
    const char *id = "default";
    const char *iso = qc_default_iso();
    const char *disk = qc_default_disk();
    const char *plugin = qc_default_plugin();
    const char *qemu_bin = "qemu-system-x86_64";
    const char *mem = "512M";
    int timeout_ms = 60000;
    bool wait_prompt = true;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        } else if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            iso = argv[++i];
        } else if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
            disk = argv[++i];
        } else if (strcmp(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-wait") == 0) {
            wait_prompt = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return QC_SESS_USAGE;
        } else {
            print_result(false, QC_SESS_USAGE, id, "start", NULL,
                         "unknown flag", -1);
            return QC_SESS_USAGE;
        }
    }

    /* Replace existing session with same id */
    qc_session_t old;
    if (load_session(id, &old) == 0) {
        if (pid_alive(old.pid)) {
            qc_qmp_t *q = qc_qmp_connect(old.qmp_sock);
            if (q) {
                qc_qmp_quit(q);
                qc_qmp_close(q);
            } else {
                kill(old.pid, SIGKILL);
            }
            for (int i = 0; i < 30; i++) {
                if (!pid_alive(old.pid)) {
                    break;
                }
                sleep_ms(100);
            }
            if (pid_alive(old.pid)) {
                kill(old.pid, SIGKILL);
            }
            waitpid(old.pid, NULL, 0);
        }
        remove_session_file(id);
        unlink(old.plugin_sock);
        unlink(old.qmp_sock);
    }

    struct stat st;
    if (stat(iso, &st) != 0) {
        print_result(false, QC_SESS_FAIL, id, "start", NULL, "iso not found",
                     -1);
        return QC_SESS_FAIL;
    }
    if (disk[0] && stat(disk, &st) != 0) {
        print_result(false, QC_SESS_FAIL, id, "start", NULL, "disk not found",
                     -1);
        return QC_SESS_FAIL;
    }
    if (stat(plugin, &st) != 0) {
        print_result(false, QC_SESS_FAIL, id, "start", NULL,
                     "plugin not found (make plugin)", -1);
        return QC_SESS_FAIL;
    }

    long long t0 = now_ms();
    qc_session_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.id, sizeof(s.id), "%s", id);
    snprintf(s.iso, sizeof(s.iso), "%s", iso);
    snprintf(s.disk, sizeof(s.disk), "%s", disk);
    snprintf(s.plugin_sock, sizeof(s.plugin_sock),
             "%s/qemu-connect-sess-%s.sock", runtime_dir(), id);
    snprintf(s.qmp_sock, sizeof(s.qmp_sock), "%s/qemu-connect-sess-%s.qmp",
             runtime_dir(), id);
    snprintf(s.logpath, sizeof(s.logpath), "%s/qemu-connect-sess-%s.log",
             runtime_dir(), id);
    unlink(s.plugin_sock);
    unlink(s.qmp_sock);

    char plugin_arg[640];
    snprintf(plugin_arg, sizeof(plugin_arg), "%s,socket=%s", plugin,
             s.plugin_sock);
    char qmp_arg[320];
    snprintf(qmp_arg, sizeof(qmp_arg), "unix:%s,server,nowait", s.qmp_sock);

    pid_t pid = fork();
    if (pid < 0) {
        print_result(false, QC_SESS_FAIL, id, "start", NULL, "fork failed", -1);
        return QC_SESS_FAIL;
    }
    if (pid == 0) {
        int logfd = open(s.logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            close(logfd);
        }
        if (disk[0]) {
            char d0[640], d1[640];
            snprintf(d0, sizeof(d0),
                     "format=raw,file=%s,if=ide,index=0,media=disk", disk);
            snprintf(d1, sizeof(d1),
                     "format=raw,file=%s,if=ide,index=1,media=cdrom", iso);
            execlp(qemu_bin, qemu_bin, "-display", "none", "-m", mem, "-accel",
                   "tcg", "-drive", d0, "-drive", d1, "-boot", "order=d",
                   "-plugin", plugin_arg, "-qmp", qmp_arg, "-serial", "none",
                   "-parallel", "none", "-monitor", "none", "-nographic",
                   (char *)NULL);
        } else {
            execlp(qemu_bin, qemu_bin, "-display", "none", "-m", mem, "-accel",
                   "tcg", "-cdrom", iso, "-boot", "order=d", "-plugin",
                   plugin_arg, "-qmp", qmp_arg, "-serial", "none", "-parallel",
                   "none", "-monitor", "none", "-nographic", (char *)NULL);
        }
        _exit(127);
    }
    s.pid = pid;

    for (int i = 0; i < 100; i++) {
        if (access(s.plugin_sock, F_OK) == 0) {
            break;
        }
        if (!pid_alive(pid)) {
            print_result(false, QC_SESS_FAIL, id, "start", NULL,
                         "QEMU exited early", now_ms() - t0);
            return QC_SESS_FAIL;
        }
        sleep_ms(100);
    }
    if (access(s.plugin_sock, F_OK) != 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        print_result(false, QC_SESS_CONNECT, id, "start", NULL,
                     "plugin socket missing", now_ms() - t0);
        return QC_SESS_CONNECT;
    }

    /* Ensure QMP is up (optional for start success if plugin works) */
    for (int i = 0; i < 50 && access(s.qmp_sock, F_OK) != 0; i++) {
        sleep_ms(100);
    }

    if (wait_prompt) {
        if (wait_expect(s.plugin_sock, "munux>", timeout_ms) != 0) {
            char text[4096] = "";
            get_console_text(s.plugin_sock, text, sizeof(text));
            /* still save session so user can inspect/stop */
            save_session(&s);
            print_result(false, QC_SESS_FAIL, id, "start", text,
                         "timeout waiting for munux>", now_ms() - t0);
            return QC_SESS_FAIL;
        }
    }

    if (save_session(&s) != 0) {
        print_result(false, QC_SESS_FAIL, id, "start", NULL,
                     "failed to write session file", now_ms() - t0);
        return QC_SESS_FAIL;
    }

    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    text[0] = '\0';
    get_console_text(s.plugin_sock, text, sizeof(text));

    printf("{\"ok\":true,\"exit_code\":0,\"op\":\"start\",\"session_id\":");
    json_escape_print(stdout, id);
    printf(",\"pid\":%d,\"plugin_sock\":", (int)s.pid);
    json_escape_print(stdout, s.plugin_sock);
    printf(",\"qmp_sock\":");
    json_escape_print(stdout, s.qmp_sock);
    printf(",\"duration_ms\":%lld,\"console\":", now_ms() - t0);
    json_escape_print(stdout, text);
    printf("}\n");
    return QC_SESS_OK;
}

static int require_session(const char *id, qc_session_t *s, const char *op)
{
    if (load_session(id, s) != 0) {
        print_result(false, QC_SESS_GONE, id, op, NULL, "no such session", -1);
        return -1;
    }
    if (!pid_alive(s->pid)) {
        remove_session_file(id);
        print_result(false, QC_SESS_GONE, id, op, NULL, "session QEMU dead",
                     -1);
        return -1;
    }
    return 0;
}

static int session_cmd(int argc, char **argv)
{
    const char *id = "default";
    int timeout_ms = 30000;
    /* collect shell words until flags */
    char line[512];
    line[0] = '\0';
    int words = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            print_result(false, QC_SESS_USAGE, id, "cmd", NULL, "unknown flag",
                         -1);
            return QC_SESS_USAGE;
        } else {
            if (line[0]) {
                strncat(line, " ", sizeof(line) - strlen(line) - 1);
            }
            strncat(line, argv[i], sizeof(line) - strlen(line) - 1);
            words++;
        }
    }
    if (words == 0) {
        print_result(false, QC_SESS_USAGE, id, "cmd", NULL,
                     "missing shell command", -1);
        return QC_SESS_USAGE;
    }

    long long t0 = now_ms();
    qc_session_t s;
    if (require_session(id, &s, "cmd") != 0) {
        return QC_SESS_GONE;
    }

    qc_qmp_t *q = qc_qmp_connect(s.qmp_sock);
    if (!q) {
        print_result(false, QC_SESS_CONNECT, id, "cmd", NULL, "qmp connect failed",
                     now_ms() - t0);
        return QC_SESS_CONNECT;
    }

    if (qc_qmp_type(q, line, 15) != 0 || qc_qmp_send_key(q, "ret") != 0) {
        qc_qmp_close(q);
        print_result(false, QC_SESS_FAIL, id, "cmd", NULL, "type failed",
                     now_ms() - t0);
        return QC_SESS_FAIL;
    }
    qc_qmp_close(q);

    sleep_ms(150);
    if (wait_expect(s.plugin_sock, "munux>", timeout_ms) != 0) {
        char text[4096] = "";
        get_console_text(s.plugin_sock, text, sizeof(text));
        print_result(false, QC_SESS_FAIL, id, "cmd", text,
                     "timeout waiting for prompt after cmd", now_ms() - t0);
        return QC_SESS_FAIL;
    }

    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    text[0] = '\0';
    get_console_text(s.plugin_sock, text, sizeof(text));

    printf("{\"ok\":true,\"exit_code\":0,\"op\":\"cmd\",\"session_id\":");
    json_escape_print(stdout, id);
    printf(",\"cmd\":");
    json_escape_print(stdout, line);
    printf(",\"duration_ms\":%lld,\"console\":", now_ms() - t0);
    json_escape_print(stdout, text);
    printf("}\n");
    return QC_SESS_OK;
}

static int session_expect(int argc, char **argv)
{
    const char *id = "default";
    const char *needle = NULL;
    int timeout_ms = 60000;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
        } else if (!needle && argv[i][0] != '-') {
            needle = argv[i];
        }
    }
    if (!needle) {
        print_result(false, QC_SESS_USAGE, id, "expect", NULL, "missing text",
                     -1);
        return QC_SESS_USAGE;
    }
    long long t0 = now_ms();
    qc_session_t s;
    if (require_session(id, &s, "expect") != 0) {
        return QC_SESS_GONE;
    }
    char text[4096] = "";
    if (wait_expect(s.plugin_sock, needle, timeout_ms) != 0) {
        get_console_text(s.plugin_sock, text, sizeof(text));
        print_result(false, QC_SESS_FAIL, id, "expect", text, "timeout",
                     now_ms() - t0);
        return QC_SESS_FAIL;
    }
    get_console_text(s.plugin_sock, text, sizeof(text));
    print_result(true, 0, id, "expect", text, NULL, now_ms() - t0);
    return QC_SESS_OK;
}

static int session_console(int argc, char **argv)
{
    const char *id = "default";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        }
    }
    qc_session_t s;
    if (require_session(id, &s, "console") != 0) {
        return QC_SESS_GONE;
    }
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    text[0] = '\0';
    if (get_console_text(s.plugin_sock, text, sizeof(text)) != 0) {
        print_result(false, QC_SESS_CONNECT, id, "console", NULL,
                     "get_console failed", -1);
        return QC_SESS_CONNECT;
    }
    print_result(true, 0, id, "console", text, NULL, -1);
    return QC_SESS_OK;
}

static int session_status(int argc, char **argv)
{
    const char *id = "default";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        }
    }
    qc_session_t s;
    if (require_session(id, &s, "status") != 0) {
        return QC_SESS_GONE;
    }
    char guest[8192] = "{}";
    char raw[8192];
    if (plugin_request(s.plugin_sock, "{\"cmd\":\"status\"}", raw, sizeof(raw)) ==
        0) {
        snprintf(guest, sizeof(guest), "%s", raw);
        /* strip trailing newline */
        size_t n = strlen(guest);
        while (n > 0 && (guest[n - 1] == '\n' || guest[n - 1] == '\r')) {
            guest[--n] = '\0';
        }
    }
    printf("{\"ok\":true,\"exit_code\":0,\"op\":\"status\",\"session_id\":");
    json_escape_print(stdout, id);
    printf(",\"pid\":%d,\"alive\":true,\"plugin_sock\":", (int)s.pid);
    json_escape_print(stdout, s.plugin_sock);
    printf(",\"qmp_sock\":");
    json_escape_print(stdout, s.qmp_sock);
    printf(",\"guest_status\":%s}\n", guest[0] ? guest : "null");
    return QC_SESS_OK;
}

static int session_stop(int argc, char **argv)
{
    const char *id = "default";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = argv[++i];
        }
    }
    long long t0 = now_ms();
    qc_session_t s;
    if (load_session(id, &s) != 0) {
        print_result(true, 0, id, "stop", NULL, "no session (already stopped)",
                     0);
        return QC_SESS_OK;
    }
    if (pid_alive(s.pid)) {
        qc_qmp_t *q = qc_qmp_connect(s.qmp_sock);
        if (q) {
            qc_qmp_quit(q);
            qc_qmp_close(q);
        } else {
            kill(s.pid, SIGTERM);
        }
        for (int i = 0; i < 40; i++) {
            if (!pid_alive(s.pid)) {
                break;
            }
            if (i == 30) {
                kill(s.pid, SIGKILL);
            }
            sleep_ms(100);
        }
        waitpid(s.pid, NULL, 0);
    }
    unlink(s.plugin_sock);
    unlink(s.qmp_sock);
    remove_session_file(id);
    print_result(true, 0, id, "stop", NULL, NULL, now_ms() - t0);
    return QC_SESS_OK;
}

int qc_cmd_session(int argc, char **argv)
{
    if (argc < 1) {
        usage();
        return QC_SESS_USAGE;
    }
    const char *sub = argv[0];
    if (strcmp(sub, "start") == 0) {
        return session_start(argc - 1, argv + 1);
    }
    if (strcmp(sub, "cmd") == 0) {
        return session_cmd(argc - 1, argv + 1);
    }
    if (strcmp(sub, "expect") == 0) {
        return session_expect(argc - 1, argv + 1);
    }
    if (strcmp(sub, "console") == 0) {
        return session_console(argc - 1, argv + 1);
    }
    if (strcmp(sub, "status") == 0) {
        return session_status(argc - 1, argv + 1);
    }
    if (strcmp(sub, "stop") == 0) {
        return session_stop(argc - 1, argv + 1);
    }
    if (strcmp(sub, "help") == 0 || strcmp(sub, "-h") == 0) {
        usage();
        return QC_SESS_USAGE;
    }
    fprintf(stderr, "session: unknown subcommand %s\n", sub);
    usage();
    return QC_SESS_USAGE;
}
