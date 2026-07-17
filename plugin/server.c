/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "server.h"

#include "protocol.h"
#include "qemu-connect.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct qc_server {
    int listen_fd;
    int client_fd;
    char path[108];
    qc_vga_state_t *vga;
};

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

qc_server_t *qc_server_start(const char *socket_path, qc_vga_state_t *vga)
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
    return srv;
}

void qc_server_poll(qc_server_t *srv)
{
    if (!srv || srv->listen_fd < 0) {
        return;
    }

    if (srv->client_fd < 0) {
        int c = accept(srv->listen_fd, NULL, NULL);
        if (c >= 0) {
            set_nonblock(c);
            srv->client_fd = c;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /* ignore */
        }
    }

    if (srv->client_fd < 0) {
        return;
    }

    char buf[4096];
    ssize_t n = read(srv->client_fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(srv->client_fd);
            srv->client_fd = -1;
        }
        return;
    }
    if (n == 0) {
        close(srv->client_fd);
        srv->client_fd = -1;
        return;
    }
    buf[n] = '\0';
    /* Strip trailing newline(s). */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }

    char resp[8192];
    qc_protocol_handle(buf, srv->vga, resp, sizeof(resp));
    size_t rlen = strlen(resp);
    if (rlen + 1 < sizeof(resp)) {
        resp[rlen++] = '\n';
        resp[rlen] = '\0';
    }
    (void)write(srv->client_fd, resp, rlen);
}

void qc_server_stop(qc_server_t *srv)
{
    if (!srv) {
        return;
    }
    if (srv->client_fd >= 0) {
        close(srv->client_fd);
    }
    if (srv->listen_fd >= 0) {
        close(srv->listen_fd);
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
