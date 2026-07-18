/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect run — spawn QEMU + plugin + QMP, run expects, tear down.
 */
#define _POSIX_C_SOURCE 200809L
#include "run.h"

#include "qemu-connect.h"
#include "qmp.h"

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

#define MAX_EXPECTS 32

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

static void usage(void)
{
    fprintf(stderr,
            "Usage: qemu-connect run --iso PATH [options]\n"
            "  --iso PATH              Guest ISO (required)\n"
            "  --plugin PATH           libqemu-connect.so (default: build/libqemu-connect.so)\n"
            "  --mem SIZE              QEMU -m (default: 512M)\n"
            "  --expect STR            Assert console contains STR (repeatable)\n"
            "  --timeout MS            Per-expect timeout (default: 60000)\n"
            "  --qemu PATH             qemu-system binary (default: qemu-system-x86_64)\n"
            "\n"
            "Exit: 0 ok, 1 expect fail, 2 missing iso/usage, 3 qemu crash, 4 connect fail\n");
}

int qc_cmd_run(int argc, char **argv)
{
    const char *iso = NULL;
    const char *plugin = "build/libqemu-connect.so";
    const char *mem = "512M";
    const char *qemu_bin = "qemu-system-x86_64";
    int timeout_ms = 60000;
    const char *expects[MAX_EXPECTS];
    int nexp = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            iso = argv[++i];
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
            if (nexp < MAX_EXPECTS) {
                expects[nexp++] = argv[++i];
            } else {
                ++i;
            }
        } else if (strcmp(argv[i], "--keep-qmp") == 0) {
            /* accepted, ignored for now (always cleanup sockets) */
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return QC_RUN_USAGE;
        } else {
            fprintf(stderr, "run: unknown arg %s\n", argv[i]);
            usage();
            return QC_RUN_USAGE;
        }
    }

    if (!iso) {
        usage();
        return QC_RUN_ISO_MISSING;
    }
    {
        struct stat st;
        if (stat(iso, &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(stderr, "run: ISO not found: %s\n", iso);
            return QC_RUN_ISO_MISSING;
        }
        if (stat(plugin, &st) != 0) {
            fprintf(stderr, "run: plugin not found: %s\n", plugin);
            return QC_RUN_CONNECT_FAIL;
        }
    }

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !runtime[0]) {
        runtime = getenv("TMPDIR");
    }
    if (!runtime || !runtime[0]) {
        runtime = "/tmp";
    }

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
        execlp(qemu_bin, qemu_bin, "-display", "none", "-m", mem, "-accel",
               "tcg", "-cdrom", iso, "-boot", "order=d", "-plugin", plugin_arg,
               "-qmp", qmp_arg, "-serial", "none", "-parallel", "none",
               "-monitor", "none", "-nographic", (char *)NULL);
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
            fprintf(stderr, "run: QEMU exited early (see %s)\n", logpath);
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
    bool exp_ok[MAX_EXPECTS];
    memset(exp_ok, 0, sizeof(exp_ok));

    if (nexp == 0) {
        char buf[256];
        if (plugin_request(psock, "{\"cmd\":\"ping\"}", buf, sizeof(buf)) != 0 ||
            strstr(buf, "\"ok\":true") == NULL) {
            rc = QC_RUN_CONNECT_FAIL;
        }
    } else {
        for (int i = 0; i < nexp; i++) {
            if (wait_expect(psock, expects[i], timeout_ms) == 0) {
                exp_ok[i] = true;
                fprintf(stderr, "run: matched %s\n", expects[i]);
            } else {
                fprintf(stderr, "run: TIMEOUT waiting for %s\n", expects[i]);
                rc = QC_RUN_EXPECT_FAIL;
                break;
            }
            int st = 0;
            if (waitpid(qpid, &st, WNOHANG) == qpid) {
                fprintf(stderr, "run: QEMU crashed during expect\n");
                rc = QC_RUN_QEMU_CRASH;
                break;
            }
        }
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

    printf("{\"ok\":%s,\"duration_ms\":%lld,\"expects\":[",
           rc == QC_RUN_OK ? "true" : "false", dur);
    for (int i = 0; i < nexp; i++) {
        printf("%s{\"pattern\":", i ? "," : "");
        putchar('"');
        for (const char *p = expects[i]; *p; p++) {
            if (*p == '"' || *p == '\\') {
                putchar('\\');
            }
            putchar(*p);
        }
        printf("\",\"ok\":%s}", exp_ok[i] ? "true" : "false");
    }
    printf("],\"exit_code\":%d}\n", rc);

    return rc;
}
