#pragma once

#include "types.h"

int net_tcp_accept(int server_fd, int nonblocking);
int net_tcp_listen(const char *host, int port, int nonblocking);
int net_tcp_connect(const char *host, int port, int nonblocking);
isize net_send_nonblocking(int fd, const void *ptr, size_t len);
isize net_recv_nonblocking(int fd, void *ptr, size_t len);
