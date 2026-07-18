/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal QMP client (Unix stream, JSON lines).
 * Out-of-band from the plugin control socket — QEMU's official control plane.
 */
#define _POSIX_C_SOURCE 200809L
#include "qmp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct qc_qmp {
    int fd;
};

static void sleep_ms(int ms)
{
    if (ms <= 0) {
        return;
    }
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
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

static int qmp_readline(int fd, char *buf, size_t buflen)
{
    size_t n = 0;
    while (n + 1 < buflen) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            if (n == 0) {
                return -1;
            }
            break;
        }
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            buf[n++] = c;
        }
    }
    buf[n] = '\0';
    return (int)n;
}

static int qmp_write_line(int fd, const char *line)
{
    size_t len = strlen(line);
    if (write(fd, line, len) != (ssize_t)len) {
        return -1;
    }
    if (write(fd, "\n", 1) != 1) {
        return -1;
    }
    return 0;
}

qc_qmp_t *qc_qmp_connect(const char *path)
{
    if (!path || !path[0]) {
        return NULL;
    }
    int fd = connect_unix(path);
    if (fd < 0) {
        fprintf(stderr, "qmp: connect(%s): %s\n", path, strerror(errno));
        return NULL;
    }

    qc_qmp_t *q = calloc(1, sizeof(*q));
    if (!q) {
        close(fd);
        return NULL;
    }
    q->fd = fd;

    char line[8192];
    if (qmp_readline(fd, line, sizeof(line)) < 0) {
        fprintf(stderr, "qmp: failed to read greeting\n");
        qc_qmp_close(q);
        return NULL;
    }
    if (strstr(line, "\"QMP\"") == NULL) {
        fprintf(stderr, "qmp: unexpected greeting: %s\n", line);
        qc_qmp_close(q);
        return NULL;
    }

    if (qmp_write_line(fd, "{\"execute\":\"qmp_capabilities\"}") < 0) {
        fprintf(stderr, "qmp: write capabilities failed\n");
        qc_qmp_close(q);
        return NULL;
    }
    for (;;) {
        if (qmp_readline(fd, line, sizeof(line)) < 0) {
            fprintf(stderr, "qmp: capabilities handshake failed\n");
            qc_qmp_close(q);
            return NULL;
        }
        if (strstr(line, "\"return\"") || strstr(line, "\"error\"")) {
            break;
        }
    }
    if (strstr(line, "\"error\"")) {
        fprintf(stderr, "qmp: capabilities error: %s\n", line);
        qc_qmp_close(q);
        return NULL;
    }
    return q;
}

void qc_qmp_close(qc_qmp_t *q)
{
    if (!q) {
        return;
    }
    if (q->fd >= 0) {
        close(q->fd);
    }
    free(q);
}

int qc_qmp_execute(qc_qmp_t *q, const char *cmd_json, char *out, size_t out_len)
{
    if (!q || q->fd < 0 || !cmd_json) {
        return -1;
    }
    if (qmp_write_line(q->fd, cmd_json) < 0) {
        return -1;
    }
    char line[8192];
    for (;;) {
        int n = qmp_readline(q->fd, line, sizeof(line));
        if (n < 0) {
            return -1;
        }
        if (strstr(line, "\"event\"")) {
            continue;
        }
        if (out && out_len) {
            snprintf(out, out_len, "%s", line);
        }
        if (strstr(line, "\"error\"")) {
            return 1;
        }
        if (strstr(line, "\"return\"")) {
            return 0;
        }
    }
}

int qc_qmp_quit(qc_qmp_t *q)
{
    if (!q || q->fd < 0) {
        return -1;
    }
    if (qmp_write_line(q->fd, "{\"execute\":\"quit\"}") < 0) {
        return -1;
    }
    char line[8192];
    for (int i = 0; i < 32; i++) {
        int n = qmp_readline(q->fd, line, sizeof(line));
        if (n < 0) {
            return 0;
        }
        if (strstr(line, "SHUTDOWN") || strstr(line, "\"return\"")) {
            return 0;
        }
        if (strstr(line, "\"error\"")) {
            return 1;
        }
    }
    return 0;
}

int qc_qmp_send_key(qc_qmp_t *q, const char *qcode)
{
    if (!qcode || !qcode[0]) {
        return -1;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "{\"execute\":\"send-key\",\"arguments\":{\"keys\":["
             "{\"type\":\"qcode\",\"data\":\"%s\"}]}}",
             qcode);
    char out[1024];
    return qc_qmp_execute(q, cmd, out, sizeof(out));
}

/* Fill @out with qcode for one character; return 0, or -1 if unsupported.
 * If shift is needed, *need_shift = true and out holds the base key. */
static int map_char(int ch, char *out, size_t out_len, bool *need_shift)
{
    *need_shift = false;
    if (out_len < 3) {
        return -1;
    }
    if (ch == '\n' || ch == '\r') {
        snprintf(out, out_len, "ret");
        return 0;
    }
    if (ch == '\t') {
        snprintf(out, out_len, "tab");
        return 0;
    }
    if (ch == '\b' || ch == 127) {
        snprintf(out, out_len, "backspace");
        return 0;
    }
    if (ch == ' ') {
        snprintf(out, out_len, "spc");
        return 0;
    }
    if (ch >= 'a' && ch <= 'z') {
        out[0] = (char)ch;
        out[1] = '\0';
        return 0;
    }
    if (ch >= 'A' && ch <= 'Z') {
        *need_shift = true;
        out[0] = (char)(ch - 'A' + 'a');
        out[1] = '\0';
        return 0;
    }
    if (ch >= '0' && ch <= '9') {
        out[0] = (char)ch;
        out[1] = '\0';
        return 0;
    }
    const char *sym = NULL;
    switch (ch) {
    case '-':
        sym = "minus";
        break;
    case '=':
        sym = "equal";
        break;
    case '[':
        sym = "bracket_left";
        break;
    case ']':
        sym = "bracket_right";
        break;
    case '\\':
        sym = "backslash";
        break;
    case ';':
        sym = "semicolon";
        break;
    case '\'':
        sym = "apostrophe";
        break;
    case ',':
        sym = "comma";
        break;
    case '.':
        sym = "dot";
        break;
    case '/':
        sym = "slash";
        break;
    case '`':
        sym = "grave_accent";
        break;
    default:
        return -1;
    }
    snprintf(out, out_len, "%s", sym);
    return 0;
}

const char *qc_qmp_char_to_qcode(int ch)
{
    static char buf[32];
    bool shift = false;
    if (map_char(ch, buf, sizeof(buf), &shift) != 0) {
        return NULL;
    }
    return buf;
}

int qc_qmp_type(qc_qmp_t *q, const char *text, int delay_ms)
{
    if (!text) {
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        char code[32];
        bool shift = false;
        if (map_char(*p, code, sizeof(code), &shift) != 0) {
            fprintf(stderr, "qmp: cannot map character 0x%02x\n", *p);
            return -1;
        }
        char out[1024];
        if (shift) {
            char cmd[320];
            snprintf(cmd, sizeof(cmd),
                     "{\"execute\":\"send-key\",\"arguments\":{\"keys\":["
                     "{\"type\":\"qcode\",\"data\":\"shift\"},"
                     "{\"type\":\"qcode\",\"data\":\"%s\"}]}}",
                     code);
            if (qc_qmp_execute(q, cmd, out, sizeof(out)) != 0) {
                return -1;
            }
        } else if (qc_qmp_send_key(q, code) != 0) {
            return -1;
        }
        sleep_ms(delay_ms);
    }
    return 0;
}
