/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 * gw_notify.c - see gw_notify.h.
 */
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "gw_notify.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int gw_notify_ready(void)
{
    static const char msg[] = "READY=1";
    const char *path = getenv("NOTIFY_SOCKET");
    struct sockaddr_un addr;
    size_t plen;
    socklen_t alen;
    ssize_t w;
    int fd;

    if (path == NULL || path[0] == '\0')
        return 0;   /* no notify supervisor: manual run, no-op */

    /* systemd hands out a filesystem path or an abstract-namespace
     * address prefixed '@' (a leading NUL on the wire). Anything else
     * is malformed and refused. */
    if (path[0] != '/' && path[0] != '@')
        return -1;

    plen = strlen(path);
    if (plen >= sizeof addr.sun_path)
        return -1;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, plen);
    if (addr.sun_path[0] == '@')
        addr.sun_path[0] = '\0';
    alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + plen);

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    w = sendto(fd, msg, sizeof msg - 1u, 0,
               (const struct sockaddr *)&addr, alen);
    close(fd);
    return (w == (ssize_t)(sizeof msg - 1u)) ? 0 : -1;
}
