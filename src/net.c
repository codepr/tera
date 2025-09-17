#include "net.h"
#include "logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 128

static int set_nonblocking(int fd)
{
    int flags, result;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;

    result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (result == -1)
        return -1;

    return 0;
}

int net_tcp_accept(int server_fd, int nonblocking)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    fd                = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd <= 0)
        goto err;

    if (nonblocking && set_nonblocking(fd) < 0)
        goto err;

    return fd;

err:
    log_error("server_accept -> accept() %s", strerror(errno));
    return -1;
}

int net_tcp_listen(const char *host, int port, int nonblocking)
{
    int listen_fd               = -1;
    const struct addrinfo hints = {
        .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE};
    struct addrinfo *result, *rp;
    char port_string[6];

    snprintf(port_string, 6, "%d", port);

    if (getaddrinfo(host, port_string, &hints, &result) != 0)
        return -1;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd < 0)
            continue;

        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
            return -1;

        /* Bind it to the addr:port opened on the network interface */
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; // successful bind
        close(listen_fd);
    }

    freeaddrinfo(result);
    if (!rp)
        return -1;

    if (nonblocking && set_nonblocking(listen_fd) < 0)
        return -1;

    if (listen(listen_fd, BACKLOG) != 0)
        return -1;

    return listen_fd;
}

int net_tcp_connect(const char *host, int port, int nonblocking)
{
    int s, retval = -1;
    struct addrinfo *servinfo, *p;
    const struct addrinfo hints = {
        .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE};

    char port_string[6];
    snprintf(port_string, sizeof(port_string), "%d", port);

    if (getaddrinfo(host, port_string, &hints, &servinfo) != 0)
        return -1;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        /* Try to create the socket and to connect it.
         * If we fail in the socket() call, or on connect(), we retry with
         * the next entry in servinfo. */
        if ((s = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;

        /* Try to connect. */
        if (connect(s, p->ai_addr, p->ai_addrlen) == -1) {
            close(s);
            break;
        }

        /* If we ended an iteration of the for loop without errors, we
         * have a connected socket. Let's return to the caller. */
        retval = s;
        break;
    }

    // Set now non-blocking so it's possible to block on the connect and have a
    // ready-to-write socket immediately
    if (nonblocking && set_nonblocking(retval) < 0)
        goto err;

    freeaddrinfo(servinfo);
    return retval; /* Will be -1 if no connection succeded. */

err:

    close(s);
    perror("socket(2) opening socket failed");
    return -1;
}

isize net_send_nonblocking(int fd, const void *ptr, size_t len)
{
    const char *buf  = ptr;
    size_t total     = 0;
    size_t bytesleft = len;
    ssize_t n        = 0;

    while (total < len) {
        n = send(fd, buf + total, bytesleft, MSG_NOSIGNAL);
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                goto err;
        }
        total += n;
        bytesleft -= n;
    }

    return total;

err:

    fprintf(stderr, "send(2) - error sending data: %s\n", strerror(errno));
    return -1;
}

isize net_recv_nonblocking(int fd, void *ptr, size_t len)
{
    char *buf     = ptr;
    ssize_t n     = 0;
    ssize_t total = 0;

    while (total < len) {
        if ((n = recv(fd, buf, len - total, 0)) < 0) {
            total = total == 0 ? -1 : total;
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                goto err;
        }

        if (n == 0) {
            return total;
        }

        buf += n;
        total += n;
    }

    return total;

err:

    fprintf(stderr, "recv(2) - error reading data: %s\n", strerror(errno));
    return -1;
}
