/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect run / guest — spawn QEMU + plugin + QMP, script steps, tear down.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "run.h"

#include "qemu-connect.h"
#include "qmp.h"
#include "paths.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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

#define MAX_STEPS 64

typedef enum { STEP_EXPECT, STEP_TYPE } step_kind_t;

typedef struct {
    step_kind_t kind;
    const char *arg;
    bool ok;
} step_t;

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

static int wait_expect(const char *psock, const char *needle, int timeout_ms)
{
    long long deadline = now_ms() + timeout_ms;
    char json[QEMU_CONNECT_RESP_MAX];
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    while (now_ms() < deadline) {
        if (plugin_request(psock, "{\"cmd\":\"get_console\"}", json,
                           sizeof(json)) == 0) {
            if (extract_text(json, text, sizeof(text)) == 0 &&
                strstr(text, needle)) {
                return 0;
            }
        }
        sleep_ms(100);
    }
    return -1;
}

static int fetch_console(const char *psock, char *text, size_t text_len)
{
    char json[QEMU_CONNECT_RESP_MAX];
    if (!text || text_len == 0) {
        return -1;
    }
    text[0] = '\0';
    if (plugin_request(psock, "{\"cmd\":\"get_console\"}", json, sizeof(json)) !=
        0) {
        return -1;
    }
    return extract_text(json, text, text_len);
}

/* Keep last @n non-blank lines of a VGA dump (n<=0 → full copy). */
static void console_tail_nonblank(const char *src, char *dst, size_t dst_len,
                                  int n)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src || !src[0] || n <= 0) {
        snprintf(dst, dst_len, "%s", src ? src : "");
        return;
    }
    /* Collect line starts */
    const char *lines[256];
    int count = 0;
    const char *p = src;
    while (*p && count < 256) {
        lines[count++] = p;
        const char *nl = strchr(p, '\n');
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    /* Mark non-blank line indices from the end */
    int kept[256];
    int k = 0;
    for (int i = count - 1; i >= 0 && k < n; i--) {
        const char *s = lines[i];
        const char *e = (i + 1 < count) ? lines[i + 1] : s + strlen(s);
        bool blank = true;
        for (const char *q = s; q < e; q++) {
            if (*q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') {
                blank = false;
                break;
            }
        }
        if (!blank) {
            kept[k++] = i;
        }
    }
    /* Reverse kept to chronological order */
    char *o = dst;
    size_t left = dst_len;
    for (int j = k - 1; j >= 0 && left > 1; j--) {
        int i = kept[j];
        const char *s = lines[i];
        const char *e = (i + 1 < count) ? lines[i + 1] : s + strlen(s);
        size_t len = (size_t)(e - s);
        if (len >= left) {
            len = left - 1;
        }
        memcpy(o, s, len);
        o += len;
        left -= len;
    }
    *o = '\0';
}

static void print_console_text(const char *text)
{
    fputs("-------- guest console --------\n", stderr);
    if (text && text[0]) {
        fputs(text, stderr);
        if (text[strlen(text) - 1] != '\n') {
            fputc('\n', stderr);
        }
    } else {
        fputs("(empty)\n", stderr);
    }
    fputs("-------------------------------\n", stderr);
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

static void real_or_copy(const char *path, char *out, size_t n)
{
    if (!path || !path[0]) {
        out[0] = '\0';
        return;
    }
    if (!realpath(path, out)) {
        snprintf(out, n, "%s", path);
    }
}

/*
 * If @disk is already open by a live qemu-connect session, fill @holder_id
 * and return 1. Returns 0 if free / unknown.
 */
static int disk_held_by_session(const char *disk, char *holder_id,
                                size_t holder_len, pid_t *holder_pid)
{
    if (!disk || !disk[0]) {
        return 0;
    }
    char want[PATH_MAX];
    real_or_copy(disk, want, sizeof(want));
    if (!want[0]) {
        return 0;
    }

    char dir[256];
    snprintf(dir, sizeof(dir), "%s/qemu-connect-sessions", runtime_dir());
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (n < 6 || strcmp(ent->d_name + n - 5, ".json") != 0) {
            continue;
        }
        char path[320];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) {
            continue;
        }
        char buf[4096];
        size_t rd = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[rd] = '\0';

        /* crude extract "disk":"..." and "id":"..." "pid":N */
        char disk_val[PATH_MAX] = "";
        char id_val[64] = "";
        long pid_val = 0;
        const char *p = strstr(buf, "\"disk\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (*p == '"') {
                    p++;
                    size_t i = 0;
                    while (*p && *p != '"' && i + 1 < sizeof(disk_val)) {
                        disk_val[i++] = *p++;
                    }
                    disk_val[i] = '\0';
                }
            }
        }
        p = strstr(buf, "\"id\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (*p == '"') {
                    p++;
                    size_t i = 0;
                    while (*p && *p != '"' && i + 1 < sizeof(id_val)) {
                        id_val[i++] = *p++;
                    }
                    id_val[i] = '\0';
                }
            }
        }
        p = strstr(buf, "\"pid\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                pid_val = strtol(p + 1, NULL, 10);
            }
        }
        if (!disk_val[0]) {
            continue;
        }
        char have[PATH_MAX];
        real_or_copy(disk_val, have, sizeof(have));
        if (strcmp(have, want) != 0) {
            continue;
        }
        if (pid_val > 0 && kill((pid_t)pid_val, 0) != 0 && errno != EPERM) {
            continue; /* stale session file */
        }
        if (holder_id && holder_len) {
            snprintf(holder_id, holder_len, "%s",
                     id_val[0] ? id_val : ent->d_name);
        }
        if (holder_pid) {
            *holder_pid = (pid_t)pid_val;
        }
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

static int log_mentions_disk_lock(const char *logpath)
{
    FILE *f = fopen(logpath, "r");
    if (!f) {
        return 0;
    }
    char line[512];
    int hit = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "write") && strstr(line, "lock")) {
            hit = 1;
            break;
        }
        if (strstr(line, "Failed to get") && strstr(line, "lock")) {
            hit = 1;
            break;
        }
    }
    fclose(f);
    return hit;
}

static void usage_run(void)
{
    fprintf(stderr,
            "Usage:\n"
            "  qemu-connect run --iso PATH [options] [steps...]\n"
            "  qemu-connect guest [shell command...]     # simple munux helper\n"
            "\n"
            "Options:\n"
            "  --iso PATH         CD/ISO (required for run)\n"
            "  --disk PATH        Optional IDE disk (e.g. munux disk.img)\n"
            "  --plugin PATH      default: build/libqemu-connect.so\n"
            "  --mem SIZE         default: 512M\n"
            "  --timeout MS       per-expect timeout (default 60000)\n"
            "  --qemu PATH        default: qemu-system-x86_64\n"
            "  --show             also print console to stderr (JSON always has console)\n"
            "  --console-lines N  JSON console: last N non-blank lines (0=full)\n"
            "\n"
            "Steps (order matters, repeatable):\n"
            "  --expect TEXT      wait until console contains TEXT\n"
            "  --type TEXT        type TEXT then Enter (needs QMP)\n"
            "\n"
            "Simple examples:\n"
            "  qemu-connect guest\n"
            "  qemu-connect guest help\n"
            "  qemu-connect guest ls\n"
            "  qemu-connect run --iso k.iso --disk d.img --expect 'munux>' \\\n"
            "      --type help --show\n"
            "\n"
            "Exit: 0 ok, 1 step fail, 2 missing iso/usage, 3 qemu crash, 4 connect fail\n");
}

static int run_session(const char *iso, const char *disk, const char *plugin,
                       const char *mem, const char *qemu_bin, int timeout_ms,
                       step_t *steps, int nsteps, bool show, int console_lines)
{
    {
        struct stat st;
        if (!iso || stat(iso, &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(stderr, "run: ISO not found: %s\n", iso ? iso : "(null)");
            return QC_RUN_ISO_MISSING;
        }
        if (disk && (stat(disk, &st) != 0 || !S_ISREG(st.st_mode))) {
            fprintf(stderr, "run: disk not found: %s\n", disk);
            return QC_RUN_ISO_MISSING;
        }
        if (stat(plugin, &st) != 0) {
            fprintf(stderr, "run: plugin not found: %s (run: make plugin)\n",
                    plugin);
            return QC_RUN_CONNECT_FAIL;
        }
    }

    /* Fail fast if a live session already holds this disk image. */
    if (disk) {
        char holder[64];
        pid_t hpid = 0;
        if (disk_held_by_session(disk, holder, sizeof(holder), &hpid)) {
            fprintf(stderr,
                    "run: disk locked by session %s (pid %d)\n"
                    "     disk=%s\n"
                    "     stop it: qemu-connect session stop --id %s\n",
                    holder, (int)hpid, disk, holder);
            printf("{\"ok\":false,\"exit_code\":%d,\"error\":\"disk locked by "
                   "session %s\",\"session_id\":\"%s\",\"pid\":%d,\"disk\":",
                   QC_RUN_QEMU_CRASH, holder, holder, (int)hpid);
            json_escape_print(stdout, disk);
            printf("}\n");
            return QC_RUN_QEMU_CRASH;
        }
    }

    const char *runtime = runtime_dir();

    char psock[256], qsock[256], logpath[256];
    pid_t self = getpid();
    snprintf(psock, sizeof(psock), "%s/qemu-connect-run-%d.sock", runtime,
             (int)self);
    snprintf(qsock, sizeof(qsock), "%s/qemu-connect-run-%d.qmp", runtime,
             (int)self);
    snprintf(logpath, sizeof(logpath), "%s/qemu-connect-run-%d.log", runtime,
             (int)self);
    unlink(psock);
    unlink(qsock);

    char plugin_arg[512];
    snprintf(plugin_arg, sizeof(plugin_arg), "%s,socket=%s", plugin, psock);
    char qmp_arg[320];
    snprintf(qmp_arg, sizeof(qmp_arg), "unix:%s,server,nowait", qsock);

    long long t0 = now_ms();
    pid_t qpid = fork();
    if (qpid < 0) {
        perror("fork");
        return QC_RUN_QEMU_CRASH;
    }
    if (qpid == 0) {
        int logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }
        if (disk) {
            char d0[512], d1[512];
            snprintf(d0, sizeof(d0), "format=raw,file=%s,if=ide,index=0,media=disk",
                     disk);
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
        perror("execlp qemu");
        _exit(127);
    }

    bool have_sock = false;
    for (int i = 0; i < 100; i++) {
        if (access(psock, F_OK) == 0) {
            have_sock = true;
            break;
        }
        int st = 0;
        if (waitpid(qpid, &st, WNOHANG) == qpid) {
            char holder[64] = "";
            pid_t hpid = 0;
            bool locked = log_mentions_disk_lock(logpath);
            if (locked && disk) {
                disk_held_by_session(disk, holder, sizeof(holder), &hpid);
            }
            if (locked) {
                if (holder[0]) {
                    fprintf(stderr,
                            "run: disk locked by session %s (pid %d)\n"
                            "     disk=%s\n"
                            "     stop it: qemu-connect session stop --id %s\n"
                            "     (qemu log: %s)\n",
                            holder, (int)hpid, disk ? disk : "?", holder,
                            logpath);
                    printf("{\"ok\":false,\"exit_code\":%d,\"error\":\"disk "
                           "locked by session %s\",\"session_id\":\"%s\","
                           "\"pid\":%d,\"disk\":",
                           QC_RUN_QEMU_CRASH, holder, holder, (int)hpid);
                    json_escape_print(stdout, disk ? disk : "");
                    printf(",\"log\":");
                    json_escape_print(stdout, logpath);
                    printf("}\n");
                } else {
                    fprintf(stderr,
                            "run: disk image is locked (another QEMU holds it)\n"
                            "     disk=%s\n"
                            "     log: %s\n"
                            "     tip: qemu-connect session stop   # or kill "
                            "other qemu-system-x86_64\n",
                            disk ? disk : "?", logpath);
                    printf("{\"ok\":false,\"exit_code\":%d,\"error\":\"disk "
                           "image locked by another QEMU process\",\"disk\":",
                           QC_RUN_QEMU_CRASH);
                    json_escape_print(stdout, disk ? disk : "");
                    printf(",\"log\":");
                    json_escape_print(stdout, logpath);
                    printf("}\n");
                }
            } else {
                fprintf(stderr, "run: QEMU exited early (log: %s)\n", logpath);
                printf("{\"ok\":false,\"exit_code\":%d,\"error\":\"QEMU exited "
                       "early\",\"log\":",
                       QC_RUN_QEMU_CRASH);
                json_escape_print(stdout, logpath);
                printf("}\n");
            }
            return QC_RUN_QEMU_CRASH;
        }
        sleep_ms(100);
    }
    if (!have_sock) {
        fprintf(stderr, "run: plugin socket not created\n");
        kill(qpid, SIGKILL);
        waitpid(qpid, NULL, 0);
        return QC_RUN_CONNECT_FAIL;
    }

    qc_qmp_t *qmp = NULL;
    for (int i = 0; i < 50; i++) {
        if (access(qsock, F_OK) == 0) {
            qmp = qc_qmp_connect(qsock);
            if (qmp) {
                break;
            }
        }
        sleep_ms(100);
    }
    if (!qmp) {
        fprintf(stderr, "run: QMP connect failed\n");
        kill(qpid, SIGKILL);
        waitpid(qpid, NULL, 0);
        unlink(psock);
        unlink(qsock);
        return QC_RUN_CONNECT_FAIL;
    }

    int rc = QC_RUN_OK;

    if (nsteps == 0) {
        char buf[256];
        if (plugin_request(psock, "{\"cmd\":\"ping\"}", buf, sizeof(buf)) != 0 ||
            strstr(buf, "\"ok\":true") == NULL) {
            rc = QC_RUN_CONNECT_FAIL;
        }
    }

    for (int i = 0; i < nsteps && rc == QC_RUN_OK; i++) {
        int st = 0;
        if (waitpid(qpid, &st, WNOHANG) == qpid) {
            fprintf(stderr, "run: QEMU crashed\n");
            rc = QC_RUN_QEMU_CRASH;
            break;
        }
        if (steps[i].kind == STEP_EXPECT) {
            fprintf(stderr, "run: wait for %s\n", steps[i].arg);
            if (wait_expect(psock, steps[i].arg, timeout_ms) == 0) {
                steps[i].ok = true;
                fprintf(stderr, "run: ok  (found)\n");
            } else {
                fprintf(stderr, "run: TIMEOUT waiting for: %s\n", steps[i].arg);
                rc = QC_RUN_EXPECT_FAIL;
                break;
            }
        } else { /* TYPE — always ends with Enter (shell line semantics) */
            fprintf(stderr, "run: type %s (+Enter)\n", steps[i].arg);
            if (qc_qmp_type_line(qmp, steps[i].arg, 15, true) != 0) {
                fprintf(stderr, "run: type failed (chars or Enter)\n");
                rc = QC_RUN_EXPECT_FAIL;
                break;
            }
            steps[i].ok = true;
            sleep_ms(200); /* let guest paint */
        }
    }

    /* Always capture final console for JSON (agents need it even on success). */
    char console_full[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
                      QEMU_CONNECT_VGA_ROWS + 8];
    char console_out[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
                     QEMU_CONNECT_VGA_ROWS + 8];
    console_full[0] = '\0';
    console_out[0] = '\0';
    fetch_console(psock, console_full, sizeof(console_full));
    console_tail_nonblank(console_full, console_out, sizeof(console_out),
                          console_lines);

    if (show || rc != QC_RUN_OK) {
        print_console_text(console_full);
    }

    long long dur = now_ms() - t0;

    qc_qmp_quit(qmp);
    qc_qmp_close(qmp);

    for (int i = 0; i < 50; i++) {
        int st = 0;
        if (waitpid(qpid, &st, WNOHANG) == qpid) {
            break;
        }
        if (i == 40) {
            kill(qpid, SIGTERM);
        }
        if (i == 45) {
            kill(qpid, SIGKILL);
        }
        sleep_ms(100);
    }
    waitpid(qpid, NULL, 0);
    unlink(psock);
    unlink(qsock);

    /* Compact JSON for agents — always include console */
    printf("{\"ok\":%s,\"duration_ms\":%lld,\"steps\":[",
           rc == QC_RUN_OK ? "true" : "false", dur);
    for (int i = 0; i < nsteps; i++) {
        printf("%s{\"op\":\"%s\",\"arg\":", i ? "," : "",
               steps[i].kind == STEP_EXPECT ? "expect" : "type");
        putchar('"');
        for (const char *p = steps[i].arg; *p; p++) {
            if (*p == '"' || *p == '\\') {
                putchar('\\');
            }
            putchar(*p);
        }
        printf("\",\"ok\":%s}", steps[i].ok ? "true" : "false");
    }
    printf("],\"exit_code\":%d,\"console\":", rc);
    json_escape_print(stdout, console_out);
    printf("}\n");
    return rc;
}

int qc_cmd_run(int argc, char **argv)
{
    const char *iso = NULL;
    const char *disk = NULL;
    const char *plugin = qc_default_plugin();
    const char *mem = "512M";
    const char *qemu_bin = "qemu-system-x86_64";
    int timeout_ms = 60000;
    int console_lines = 0; /* 0 = full console */
    bool show = false;
    step_t steps[MAX_STEPS];
    int nsteps = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            iso = argv[++i];
        } else if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) {
            disk = argv[++i];
        } else if (strcmp(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin = argv[++i];
        } else if (strcmp(argv[i], "--mem") == 0 && i + 1 < argc) {
            mem = argv[++i];
        } else if (strcmp(argv[i], "--qemu") == 0 && i + 1 < argc) {
            qemu_bin = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
            if (timeout_ms <= 0) {
                timeout_ms = 60000;
            }
        } else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
            if (nsteps < MAX_STEPS) {
                steps[nsteps].kind = STEP_EXPECT;
                steps[nsteps].arg = argv[++i];
                steps[nsteps].ok = false;
                nsteps++;
            } else {
                ++i;
            }
        } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            if (nsteps < MAX_STEPS) {
                steps[nsteps].kind = STEP_TYPE;
                steps[nsteps].arg = argv[++i];
                steps[nsteps].ok = false;
                nsteps++;
            } else {
                ++i;
            }
        } else if (strcmp(argv[i], "--show") == 0 ||
                   strcmp(argv[i], "--console") == 0) {
            show = true;
        } else if (strcmp(argv[i], "--console-lines") == 0 && i + 1 < argc) {
            console_lines = atoi(argv[++i]);
            if (console_lines < 0) {
                console_lines = 0;
            }
        } else if (strcmp(argv[i], "--keep-qmp") == 0) {
            /* ignored */
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage_run();
            return QC_RUN_USAGE;
        } else {
            fprintf(stderr, "run: unknown arg %s\n", argv[i]);
            usage_run();
            return QC_RUN_USAGE;
        }
    }

    if (!iso) {
        usage_run();
        return QC_RUN_ISO_MISSING;
    }
    return run_session(iso, disk, plugin, mem, qemu_bin, timeout_ms, steps,
                       nsteps, show, console_lines);
}

/*
 * guest [cmd...]
 *   Boots munux/KFS with defaults, waits for shell prompt, optional cmd, shows console.
 *   Prompt: QEMU_CONNECT_PROMPT (default "munux>"); KFS often uses "$".
 */
int qc_cmd_guest(int argc, char **argv)
{
    const char *iso = qc_default_iso();
    const char *disk = qc_default_disk();
    const char *plugin = qc_default_plugin();
    /* Force path init so .qemu-connect.local can set QEMU_CONNECT_PROMPT */
    (void)qc_default_iso();
    const char *prompt = getenv("QEMU_CONNECT_PROMPT");
    if (!prompt || !prompt[0]) {
        prompt = "munux>";
    }
    int timeout_ms = 60000;
    int console_lines = 0;

    /* optional overrides as flags before command words */
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if ((strcmp(argv[i], "--iso") == 0 || strcmp(argv[i], "--disk") == 0 ||
             strcmp(argv[i], "--plugin") == 0 ||
             strcmp(argv[i], "--prompt") == 0 ||
             strcmp(argv[i], "--console-lines") == 0 ||
             strcmp(argv[i], "--timeout") == 0) &&
            i + 1 >= argc) {
            fprintf(stderr, "guest: %s needs a value\n", argv[i]);
            return QC_RUN_USAGE;
        }
        if (strcmp(argv[i], "--iso") == 0) {
            iso = argv[++i];
        } else if (strcmp(argv[i], "--disk") == 0) {
            disk = argv[++i];
        } else if (strcmp(argv[i], "--plugin") == 0) {
            plugin = argv[++i];
        } else if (strcmp(argv[i], "--prompt") == 0) {
            prompt = argv[++i];
        } else if (strcmp(argv[i], "--console-lines") == 0) {
            console_lines = atoi(argv[++i]);
            if (console_lines < 0) {
                console_lines = 0;
            }
        } else if (strcmp(argv[i], "--timeout") == 0) {
            timeout_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "Usage: qemu-connect guest [shell words...]\n"
                    "  Boots munux/KFS (iso+disk), waits for shell prompt,\n"
                    "  types the shell command if given, prints console.\n"
                    "  Success JSON always includes \"console\".\n"
                    "Env: QEMU_CONNECT_MUNUX=/path/to/kernel (or .qemu-connect.local)\n"
                    "     QEMU_CONNECT_PROMPT=munux> or $\n"
                    "Flags: --console-lines N  (last N non-blank lines in JSON)\n"
                    "Examples:\n"
                    "  qemu-connect guest help\n"
                    "  qemu-connect guest --prompt '$' help\n"
                    "  qemu-connect guest --iso /path/kernel.iso --disk /path/disk.img ls\n");
            return QC_RUN_USAGE;
        } else {
            fprintf(stderr, "guest: unknown flag %s\n", argv[i]);
            return QC_RUN_USAGE;
        }
        i++;
    }

    step_t steps[MAX_STEPS];
    int nsteps = 0;
    steps[nsteps++] =
        (step_t){ .kind = STEP_EXPECT, .arg = prompt, .ok = false };

    if (i < argc) {
        /* Join remaining args into one shell line */
        static char line[512];
        line[0] = '\0';
        for (; i < argc; i++) {
            if (line[0]) {
                strncat(line, " ", sizeof(line) - strlen(line) - 1);
            }
            strncat(line, argv[i], sizeof(line) - strlen(line) - 1);
        }
        steps[nsteps++] =
            (step_t){ .kind = STEP_TYPE, .arg = line, .ok = false };
        /* After a command, wait for the prompt to return */
        steps[nsteps++] =
            (step_t){ .kind = STEP_EXPECT, .arg = prompt, .ok = false };
    }

    fprintf(stderr, "guest: munux  iso=%s  disk=%s  prompt=%s\n", iso, disk,
            prompt);
    return run_session(iso, disk, plugin, "512M", "qemu-system-x86_64",
                       timeout_ms, steps, nsteps, true /* stderr show */,
                       console_lines);
}
