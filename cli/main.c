/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect CLI — talk to the plugin control socket.
 *
 *   qemu-connect [--socket PATH] ping|version|get_console|expect|raw ...
 */
#define _POSIX_C_SOURCE 200809L
#include "qemu-connect.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [--socket PATH] <command> [args]\n"
            "Commands:\n"
            "  ping\n"
            "  version\n"
            "  get_console [--text-only] [--refresh]\n"
            "  expect <substring> [--timeout MS]\n"
            "  raw <json-line>\n"
            "\n"
            "Default socket: %s\n"
            "Default expect timeout: 30000 ms\n",
            argv0, QEMU_CONNECT_DEFAULT_SOCK);
    return 2;
}

static int connect_sock(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "connect(%s): %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int request_buf(const char *path, const char *line, char *buf,
                       size_t buf_len, ssize_t *out_n)
{
    int fd = connect_sock(path);
    if (fd < 0) {
        return 1;
    }

    char req[4096];
    snprintf(req, sizeof(req), "%s\n", line);
    if (write(fd, req, strlen(req)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    size_t total = 0;
    while (total + 1 < buf_len) {
        ssize_t n = read(fd, buf + total, buf_len - 1 - total);
        if (n < 0) {
            perror("read");
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        if (memchr(buf, '\n', total)) {
            break;
        }
    }
    close(fd);
    buf[total] = '\0';
    if (out_n) {
        *out_n = (ssize_t)total;
    }
    return 0;
}

static int request(const char *path, const char *line)
{
    char buf[QEMU_CONNECT_RESP_MAX];
    ssize_t n = 0;
    if (request_buf(path, line, buf, sizeof(buf), &n) != 0) {
        return 1;
    }
    fputs(buf, stdout);
    if (n == 0 || buf[n - 1] != '\n') {
        fputc('\n', stdout);
    }
    return 0;
}

/* Extract JSON string field "text":"..." into dst (unescaped). */
static int extract_text_field(const char *json, char *dst, size_t dst_len)
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
            case 'u':
                for (int i = 0; i < 4 && p[1]; i++) {
                    p++;
                }
                dst[o++] = '?';
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

static int print_text_only(const char *json)
{
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];
    if (extract_text_field(json, text, sizeof(text)) != 0) {
        fprintf(stderr, "get_console: no text field in response\n");
        fputs(json, stderr);
        return 1;
    }
    fputs(text, stdout);
    fputc('\n', stdout);
    return 0;
}

static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

static int cmd_expect(const char *sock, const char *needle, int timeout_ms)
{
    if (!needle || !needle[0]) {
        fprintf(stderr, "expect: empty substring\n");
        return 2;
    }

    long long deadline = now_ms() + timeout_ms;
    char json[QEMU_CONNECT_RESP_MAX];
    char text[QEMU_CONNECT_VGA_COLS * QEMU_CONNECT_VGA_ROWS +
              QEMU_CONNECT_VGA_ROWS + 8];

    while (now_ms() < deadline) {
        if (request_buf(sock, "{\"cmd\":\"get_console\"}", json, sizeof(json),
                        NULL) != 0) {
            sleep_ms(100);
            continue;
        }
        if (extract_text_field(json, text, sizeof(text)) != 0) {
            sleep_ms(100);
            continue;
        }
        if (strstr(text, needle) != NULL) {
            fprintf(stderr, "expect: matched %s\n", needle);
            return 0;
        }
        sleep_ms(100);
    }

    fprintf(stderr, "expect: timeout after %d ms waiting for: %s\n", timeout_ms,
            needle);
    /* Best-effort dump for agents */
    if (request_buf(sock, "{\"cmd\":\"get_console\"}", json, sizeof(json),
                    NULL) == 0) {
        if (extract_text_field(json, text, sizeof(text)) == 0) {
            fprintf(stderr, "--- last console ---\n%s\n", text);
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *sock = QEMU_CONNECT_DEFAULT_SOCK;
    int i = 1;

    if (argc < 2) {
        return usage(argv[0]);
    }

    /* Global optional --socket before command */
    if (strcmp(argv[i], "--socket") == 0) {
        if (argc < 4) {
            return usage(argv[0]);
        }
        sock = argv[i + 1];
        i += 2;
    }

    if (i >= argc) {
        return usage(argv[0]);
    }

    const char *cmd = argv[i];

    if (strcmp(cmd, "ping") == 0) {
        return request(sock, "{\"cmd\":\"ping\"}");
    }
    if (strcmp(cmd, "version") == 0) {
        return request(sock, "{\"cmd\":\"version\"}");
    }
    if (strcmp(cmd, "get_console") == 0) {
        bool text_only = false;
        bool refresh = false;
        for (int j = i + 1; j < argc; j++) {
            if (strcmp(argv[j], "--text-only") == 0) {
                text_only = true;
            } else if (strcmp(argv[j], "--refresh") == 0) {
                refresh = true;
            } else if (strcmp(argv[j], "--socket") == 0 && j + 1 < argc) {
                sock = argv[++j];
            }
        }
        const char *line =
            refresh ? "{\"cmd\":\"get_console\",\"refresh\":true}"
                    : "{\"cmd\":\"get_console\"}";
        if (!text_only) {
            return request(sock, line);
        }
        char buf[QEMU_CONNECT_RESP_MAX];
        if (request_buf(sock, line, buf, sizeof(buf), NULL) != 0) {
            return 1;
        }
        /* refresh errors have no text field */
        if (strstr(buf, "\"ok\":false")) {
            fputs(buf, stderr);
            return 1;
        }
        return print_text_only(buf);
    }
    if (strcmp(cmd, "expect") == 0) {
        if (i + 1 >= argc) {
            return usage(argv[0]);
        }
        const char *needle = argv[i + 1];
        int timeout_ms = 30000;
        for (int j = i + 2; j < argc; j++) {
            if (strcmp(argv[j], "--timeout") == 0 && j + 1 < argc) {
                timeout_ms = atoi(argv[++j]);
                if (timeout_ms <= 0) {
                    timeout_ms = 30000;
                }
            } else if (strcmp(argv[j], "--socket") == 0 && j + 1 < argc) {
                sock = argv[++j];
            }
        }
        return cmd_expect(sock, needle, timeout_ms);
    }
    if (strcmp(cmd, "raw") == 0) {
        if (i + 1 >= argc) {
            return usage(argv[0]);
        }
        return request(sock, argv[i + 1]);
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 ||
        strcmp(cmd, "--help") == 0) {
        return usage(argv[0]);
    }

    fprintf(stderr, "unknown command: %s\n", cmd);
    return usage(argv[0]);
}
