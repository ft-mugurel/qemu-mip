/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Control-plane Unix socket server.
 *
 * Default path: dedicated pthread + poll() so agents can ping while the guest
 * is in hlt / not translating. Optional socket_thread=off falls back to
 * qc_server_poll() from a TB callback.
 *
 * Framing: newline-delimited JSON, one request → one response, max 8 KiB/line.
 */
#define _GNU_SOURCE
#include "server.h"

#include "protocol.h"
#include "qemu-connect.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

struct qc_server {
    int listen_fd;
    int client_fd;
    char path[108];
    qc_vga_state_t *vga;

    bool use_thread;
    bool thread_started;
    volatile bool stop;
    pthread_t thread;

    /* Accumulator for the current request line (no pipelining). */
    char line_buf[QC_SERVER_MAX_LINE];
    size_t line_len;
};

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_client(qc_server_t *srv)
{
    if (srv->client_fd >= 0) {
        close(srv->client_fd);
        srv->client_fd = -1;
    }
    srv->line_len = 0;
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd p = { .fd = fd, .events = POLLOUT };
                if (poll(&p, 1, 1000) <= 0) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static void handle_complete_line(qc_server_t *srv)
{
    /* Strip trailing CR if present (already stopped at LF). */
    size_t n = srv->line_len;
    while (n > 0 && (srv->line_buf[n - 1] == '\r' || srv->line_buf[n - 1] == '\n')) {
        n--;
    }
    srv->line_buf[n] = '\0';

    char resp[QEMU_CONNECT_RESP_MAX];
    qc_protocol_handle(srv->line_buf, srv->vga, resp, sizeof(resp));

    size_t rlen = strlen(resp);
    if (rlen + 1 < sizeof(resp)) {
        resp[rlen++] = '\n';
        resp[rlen] = '\0';
    }

    if (write_all(srv->client_fd, resp, rlen) < 0) {
        close_client(srv);
    }
    srv->line_len = 0;
}

static void feed_client_bytes(qc_server_t *srv, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            handle_complete_line(srv);
            if (srv->client_fd < 0) {
                return;
            }
            continue;
        }
        if (srv->line_len >= QC_SERVER_MAX_LINE) {
            /* Oversized line without newline — drop client. */
            close_client(srv);
            return;
        }
        srv->line_buf[srv->line_len++] = c;
    }
}

static void try_accept(qc_server_t *srv)
{
    for (;;) {
        int c = accept(srv->listen_fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (set_nonblock(c) < 0) {
            close(c);
            return;
        }
        /* Single client: replace previous connection. */
        if (srv->client_fd >= 0) {
            close_client(srv);
        }
        srv->client_fd = c;
        srv->line_len = 0;
        return;
    }
}

static void try_read_client(qc_server_t *srv)
{
    char buf[1024];
    for (;;) {
        ssize_t n = read(srv->client_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            close_client(srv);
            return;
        }
        if (n == 0) {
            close_client(srv);
            return;
        }
        feed_client_bytes(srv, buf, (size_t)n);
        if (srv->client_fd < 0) {
            return;
        }
        /* Keep reading while data is available. */
    }
}

/* One non-blocking progress step (used by poll fallback and thread). */
static void server_step(qc_server_t *srv, int timeout_ms)
{
    if (!srv || srv->listen_fd < 0 || srv->stop) {
        return;
    }

    struct pollfd fds[2];
    nfds_t nfds = 0;

    fds[nfds].fd = srv->listen_fd;
    fds[nfds].events = POLLIN;
    fds[nfds].revents = 0;
    nfds++;

    if (srv->client_fd >= 0) {
        fds[nfds].fd = srv->client_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    int pr = poll(fds, nfds, timeout_ms);
    if (pr < 0) {
        if (errno != EINTR) {
            /* leave for next iteration */
        }
        return;
    }
    if (pr == 0) {
        return;
    }

    if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
        if (fds[0].revents & POLLIN) {
            try_accept(srv);
        }
    }

    if (nfds > 1 && srv->client_fd >= 0) {
        if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            if (fds[1].revents & (POLLERR | POLLHUP)) {
                /* Still try read to get EOF cleanly; then close. */
            }
            if (fds[1].revents & POLLIN || fds[1].revents & (POLLERR | POLLHUP)) {
                try_read_client(srv);
            }
        }
    }
}

static void *server_thread_main(void *arg)
{
    qc_server_t *srv = arg;
    while (!srv->stop) {
        /* Short timeout so stop is noticed promptly after fds are closed. */
        server_step(srv, 200);
    }
    return NULL;
}

qc_server_t *qc_server_start(const char *socket_path, qc_vga_state_t *vga,
                             bool use_thread)
{
    const char *path = socket_path && socket_path[0]
                           ? socket_path
                           : QEMU_CONNECT_DEFAULT_SOCK;

    qc_server_t *srv = calloc(1, sizeof(*srv));
    if (!srv) {
        return NULL;
    }
    srv->listen_fd = -1;
    srv->client_fd = -1;
    srv->vga = vga;
    srv->use_thread = use_thread;
    srv->stop = false;
    srv->thread_started = false;
    srv->line_len = 0;
    snprintf(srv->path, sizeof(srv->path), "%s", path);

    unlink(srv->path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        free(srv);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", srv->path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        free(srv);
        return NULL;
    }

    /* Restrict socket to the creating user (best-effort on this FS). */
    if (chmod(srv->path, 0600) < 0) {
        /* Non-fatal: some filesystems may not honor mode on AF_UNIX. */
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(srv->path);
        free(srv);
        return NULL;
    }
    if (set_nonblock(fd) < 0) {
        close(fd);
        unlink(srv->path);
        free(srv);
        return NULL;
    }

    srv->listen_fd = fd;

    if (use_thread) {
        int err = pthread_create(&srv->thread, NULL, server_thread_main, srv);
        if (err != 0) {
            close(fd);
            unlink(srv->path);
            free(srv);
            return NULL;
        }
        srv->thread_started = true;
    }

    return srv;
}

void qc_server_poll(qc_server_t *srv)
{
    if (!srv || srv->use_thread) {
        /* Thread owns the FDs; do not race accept/read from another thread. */
        return;
    }
    server_step(srv, 0);
}

void qc_server_stop(qc_server_t *srv)
{
    if (!srv) {
        return;
    }

    srv->stop = true;

    /* Unblock poll() in the server thread. */
    if (srv->client_fd >= 0) {
        shutdown(srv->client_fd, SHUT_RDWR);
        close(srv->client_fd);
        srv->client_fd = -1;
    }
    if (srv->listen_fd >= 0) {
        shutdown(srv->listen_fd, SHUT_RDWR);
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }

    if (srv->thread_started) {
        pthread_join(srv->thread, NULL);
        srv->thread_started = false;
    }

    if (srv->path[0]) {
        unlink(srv->path);
    }
    free(srv);
}

const char *qc_server_path(const qc_server_t *srv)
{
    return srv ? srv->path : NULL;
}

bool qc_server_uses_thread(const qc_server_t *srv)
{
    return srv && srv->use_thread;
}
