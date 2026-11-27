#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stddef.h>

#if defined(HAVE_IPTOS_THROUGHPUT) && HAVE_IPTOS_THROUGHPUT
    #include <netinet/ip.h>
#endif

#if defined(HAVE_POLL) && HAVE_POLL
    #include <poll.h>
#else
    #include <sys/select.h>
#endif

struct FtpSocketPollFd {
#if defined(HAVE_POLL) && HAVE_POLL
    struct pollfd s;
#else
    int pad;
#endif
};

struct FtpSocket {
    int s;
};

static inline int ftp_socket_open_unistd(struct FtpSocket* sock, int domain, int type, int protocol) {
    return sock->s = socket(domain, type, protocol);
}

static inline int ftp_socket_recv_unistd(struct FtpSocket* sock, void* buf, size_t size, int flags) {
    return recv(sock->s, buf, size, flags);
}

static inline int ftp_socket_send_unistd(struct FtpSocket* sock, const void* buf, size_t size, int flags) {
    return send(sock->s, buf, size, flags);
}

static inline int ftp_socket_close_unistd(struct FtpSocket* sock) {
    if (sock->s) {
        shutdown(sock->s, SHUT_RDWR);
        close(sock->s);
        sock->s = 0;
    }
    return 0;
}

static inline int ftp_socket_accept_unistd(struct FtpSocket* sock_out, struct FtpSocket* listen_sock, struct sockaddr* addr, size_t* addrlen) {
    socklen_t len = *addrlen;
    const int rc = sock_out->s = accept(listen_sock->s, addr, &len);
    *addrlen = len;
    return rc;
}

static inline int ftp_socket_bind_unistd(struct FtpSocket* sock, struct sockaddr* addr, size_t addrlen) {
    return bind(sock->s, addr, addrlen);
}

static inline int ftp_socket_connect_unistd(struct FtpSocket* sock, struct sockaddr* addr, size_t addrlen) {
    return connect(sock->s, addr, addrlen);
}

static inline int ftp_socket_listen_unistd(struct FtpSocket* sock, int backlog) {
    return listen(sock->s, backlog);
}

static inline int ftp_socket_getsockname_unistd(struct FtpSocket* sock, struct sockaddr* addr, size_t* addrlen) {
    socklen_t len = *addrlen;
    const int rc = getsockname(sock->s, addr, &len);
    *addrlen = len;
    return rc;
}

static inline int ftp_socket_set_reuseaddr_enable_unistd(struct FtpSocket* sock, int enable) {
#if defined(HAVE_SO_REUSEADDR) && HAVE_SO_REUSEADDR
    const int option = 1;
    return setsockopt(sock->s, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
#else
    return 0;
#endif
}

static inline int ftp_socket_set_nodelay_enable_unistd(struct FtpSocket* sock, int enable) {
#if defined(HAVE_TCP_NODELAY) && HAVE_TCP_NODELAY
    const int option = 1;
    return setsockopt(sock->s, IPPROTO_TCP, TCP_NODELAY, &option, sizeof(option));
#else
    return 0;
#endif
}

static inline int ftp_socket_set_keepalive_enable_unistd(struct FtpSocket* sock, int enable) {
#if defined(HAVE_SO_KEEPALIVE) && HAVE_SO_KEEPALIVE
    const int option = 1;
    return setsockopt(sock->s, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(option));
#else
    return 0;
#endif
}

static inline int ftp_socket_set_throughput_enable_unistd(struct FtpSocket* sock, int enable) {
#if defined(HAVE_IPTOS_THROUGHPUT) && HAVE_IPTOS_THROUGHPUT
    const int option = IPTOS_THROUGHPUT;
    return setsockopt(sock->s, IPPROTO_IP, IP_TOS, &option, sizeof(option));
#else
    return 0;
#endif
}

/* Tune only the FTP data connection. Rejection leaves the system defaults. */
static inline void ftp_socket_set_transfer_buffers(struct FtpSocket* sock) {
    const int send_bytes = 256 * 1024;
    const int receive_sizes[] = {1024 * 1024, 512 * 1024, 256 * 1024};
    /* Keep accepting data while the FTP worker flushes an upload block. */
    for (size_t i = 0; i < sizeof(receive_sizes) / sizeof(receive_sizes[0]); ++i) {
        if (setsockopt(sock->s, SOL_SOCKET, SO_RCVBUF, &receive_sizes[i], sizeof(receive_sizes[i])) == 0)
            break;
    }
    (void)setsockopt(sock->s, SOL_SOCKET, SO_SNDBUF, &send_bytes, sizeof(send_bytes));
}

static inline int ftp_socket_set_nonblocking_enable_unistd(struct FtpSocket* sock, int enable) {
    int rc = fcntl(sock->s, F_GETFL, 0);
    if (rc >= 0) {
        rc = fcntl(sock->s, F_SETFL, rc | O_NONBLOCK);
    }
    return rc;
}

#if defined(HAVE_POLL) && HAVE_POLL
static inline int ftp_socket_poll_unistd(struct FtpSocketPollEntry* entries, struct FtpSocketPollFd* _fds, size_t nfds, int timeout) {
    struct pollfd* fds = (struct pollfd*)_fds;

    for (size_t i = 0; i < nfds; i++) {
        if (entries[i].fd) {
            fds[i].fd = entries[i].fd->s;
            fds[i].events = 0;
            if (entries[i].events & FtpSocketPollType_IN) {
                fds[i].events |= POLLIN;
            }
            if (entries[i].events & FtpSocketPollType_OUT) {
                fds[i].events |= POLLOUT;
            }
        } else {
            fds[i].fd = -1;
        }
    }

    int rc = poll(fds, nfds, timeout);
    if (rc < 0) {
        return rc;
    }

    for (size_t i = 0; i < nfds; i++) {
        if (entries[i].fd) {
            entries[i].revents = 0;
            if (fds[i].revents & POLLIN) {
                entries[i].revents |= FtpSocketPollType_IN;
            }
            if (fds[i].revents & POLLOUT) {
                entries[i].revents |= FtpSocketPollType_OUT;
            }
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                entries[i].revents |= FtpSocketPollType_ERROR;
            }
        }
    }

    return rc;
}
#else
static inline int ftp_socket_poll_unistd(struct FtpSocketPollEntry* entries, struct FtpSocketPollFd* _fds, size_t nfds, int timeout) {
    // initialise fds.
    int set_nfds = 0;
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);

    // sets an fd for r/w and efds and adjusts nfds.
    #define FD_SET_HELPER(set_nfds, fd, rwsetp) do { \
        assert(fd < FD_SETSIZE && "fd is out of range!"); \
        set_nfds = fd > set_nfds ? fd : set_nfds; \
        FD_SET(fd, rwsetp); FD_SET(fd, &efds); \
    } while (0)

    for (size_t i = 0; i < nfds; i++) {
        if (entries[i].fd) {
            if (entries[i].events & FtpSocketPollType_IN) {
                FD_SET_HELPER(set_nfds, entries[i].fd->s, &rfds);
            }
            if (entries[i].events & FtpSocketPollType_OUT) {
                FD_SET_HELPER(set_nfds, entries[i].fd->s, &wfds);
            }
        }
    }

    #undef FD_SET_HELPER

    // if -1, then set tvp to NULL to wait forever.
    struct timeval tv;
    struct timeval* tvp = NULL;
    if (timeout >= 0) {
        tvp = &tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
    }

    int rc = select(set_nfds + 1, &rfds, &wfds, &efds, tvp);
    if (rc < 0) {
        return rc;
    }

    for (size_t i = 0; i < nfds; i++) {
        if (entries[i].fd) {
            entries[i].revents = 0;
            if (FD_ISSET(entries[i].fd->s, &rfds)) {
                entries[i].revents |= FtpSocketPollType_IN;
            }
            if (FD_ISSET(entries[i].fd->s, &wfds)) {
                entries[i].revents |= FtpSocketPollType_OUT;
            }
            if (FD_ISSET(entries[i].fd->s, &efds)) {
                entries[i].revents |= FtpSocketPollType_ERROR;
            }

            if (entries[i].revents) {
                rc++;
            }
        }
    }

    return rc;
}
#endif

#define ftp_socket_open ftp_socket_open_unistd
#define ftp_socket_recv ftp_socket_recv_unistd
#define ftp_socket_send ftp_socket_send_unistd
#define ftp_socket_close ftp_socket_close_unistd
#define ftp_socket_accept ftp_socket_accept_unistd
#define ftp_socket_bind ftp_socket_bind_unistd
#define ftp_socket_connect ftp_socket_connect_unistd
#define ftp_socket_listen ftp_socket_listen_unistd
#define ftp_socket_getsockname ftp_socket_getsockname_unistd
#define ftp_socket_set_reuseaddr_enable ftp_socket_set_reuseaddr_enable_unistd
#define ftp_socket_set_nodelay_enable ftp_socket_set_nodelay_enable_unistd
#define ftp_socket_set_keepalive_enable ftp_socket_set_keepalive_enable_unistd
#define ftp_socket_set_throughput_enable ftp_socket_set_throughput_enable_unistd
#define ftp_socket_set_nonblocking_enable ftp_socket_set_nonblocking_enable_unistd
#define ftp_socket_poll ftp_socket_poll_unistd

#ifdef __cplusplus
}
#endif
