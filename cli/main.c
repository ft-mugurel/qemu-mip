/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * qemu-connect CLI — talk to the plugin control socket.
 *
 * Usage:
 *   qemu-connect [--socket PATH] ping
 *   qemu-connect [--socket PATH] version
 *   qemu-connect [--socket PATH] get_console [--text-only]
 *   qemu-connect [--socket PATH] raw '{"cmd":"ping"}'
 */
#include "qemu-connect.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [--socket PATH] <command>\n"
            "Commands:\n"
            "  ping                    Liveness check\n"
            "  version                 Protocol / name / mem counters\n"
            "  get_console             VGA console JSON (proto 0.2 includes text)\n"
            "  get_console --text-only Print only the console text field\n"
            "  raw <json-line>         Send a raw request line\n"
            "\n"
            "Default socket: %s\n",
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

/* Minimal extract of JSON string field "text":"..." with basic unescapes. */
static int print_text_only(const char *json)
{
    const char *key = "\"text\":\"";
    const char *p = strstr(json, key);
    if (!p) {
        fprintf(stderr, "get_console: no text field in response\n");
        fputs(json, stderr);
        return 1;
    }
    p += strlen(key);
    while (*p) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':
                putchar('\n');
                break;
            case 'r':
                putchar('\r');
                break;
            case 't':
                putchar('\t');
                break;
            case '"':
                putchar('"');
                break;
            case '\\':
                putchar('\\');
                break;
            case 'u':
                /* skip \uXXXX */
                for (int i = 0; i < 4 && p[1]; i++) {
                    p++;
                }
                putchar('?');
                break;
            default:
                putchar(*p);
                break;
            }
            p++;
            continue;
        }
        if (*p == '"') {
            break;
        }
        putchar(*p++);
    }
    putchar('\n');
    return 0;
}

int main(int argc, char **argv)
{
    const char *sock = QEMU_CONNECT_DEFAULT_SOCK;
    int i = 1;

    if (argc < 2) {
        return usage(argv[0]);
    }

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
        if (i + 1 < argc && strcmp(argv[i + 1], "--text-only") == 0) {
            text_only = true;
        }
        if (!text_only) {
            return request(sock, "{\"cmd\":\"get_console\"}");
        }
        char buf[QEMU_CONNECT_RESP_MAX];
        if (request_buf(sock, "{\"cmd\":\"get_console\"}", buf, sizeof(buf),
                        NULL) != 0) {
            return 1;
        }
        return print_text_only(buf);
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
