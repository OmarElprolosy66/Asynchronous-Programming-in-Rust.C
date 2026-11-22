#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

// sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct tcp_conn {
    int raw_fd;
    struct sockaddr_in addr;
} tcp_conn;

void tcp_conn_init(tcp_conn *conn, struct sockaddr_in addr) {
    int raw_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (raw_fd < 1) {
        printf("tcp_conn_init error: %s\n", strerror(errno));
        return;
    }

    conn->raw_fd = raw_fd;
    conn->addr   = addr;
}

void tcp_conn_cleanup(tcp_conn *conn) {
    int n = close(conn->raw_fd);
    if (n < 0) {
        printf("close error: %s\n", strerror(errno));
        return;
    }
}
void tcp_conn_set_nonblocking(tcp_conn *conn) {
    int flags = fcntl(conn->raw_fd, F_GETFL, 0);
    if (flags < 0) {
        printf("fcntl error: %s\n", strerror(errno));
        return;
    }

    int n = fcntl(conn->raw_fd, F_SETFL, flags | O_NONBLOCK);
    if (n < 0) {
        printf("fcntl error: %s\n", strerror(errno));
        return;
    }
}

// epoll
typedef struct Registery {
    int raw_fd;
} Registery;

typedef struct Poll {
    Registery registry;
} Poll;

void poll_init(Poll *poll) {
    poll->registry.raw_fd = epoll_create(1);
    if (poll->registry.raw_fd == -1) {
        printf("epoll_create\n");
        return;
    }
}

void registery_cleanup(Registery *registry) {
    int n = close(registry->raw_fd);
    if (n < 0) {
        printf("close error: %s\n", strerror(errno));
        return;
    }
}

void registery_add(Registery *registry, int raw_fd, size_t token, uint32_t interestes) {
    struct epoll_event event;
    event.events   = interestes;
    event.data.u64 = token;

    int n = epoll_ctl(registry->raw_fd, EPOLL_CTL_ADD, raw_fd, &event);
    if (n < 0) {
        printf("epoll_create error: %s\n", strerror(errno));
        return;
    }
}

int main() {
    Poll poll;        poll_init(&poll);
    int n_events = 8; tcp_conn conns[n_events];

    for (int i = 0; i < n_events; i++) {
        int delay = (n_events - i) * 1000;
        char req[76];
        sprintf(req, "GET /%d/request-%d HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", delay, i);


        tcp_conn conn;
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET; addr.sin_port = htons(8080);
        if ((inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr)) < 0) {
            printf("{loop: %d} inet_pton error: %s\n", i, strerror(errno));
            exit(1);
        }
        tcp_conn_init(&conn, addr);
        tcp_conn_set_nonblocking(&conn);

        if (connect(conn.raw_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
            printf("{loop: %d} conn error: %s\n", i, strerror(errno));
            exit(1);
        }

        if (send(conn.raw_fd, req, strlen(req), 0) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("{loop: %d} send error: %s\n", i, strerror(errno));
            exit(1);
        }

        registery_add(&poll.registry, conn.raw_fd, i, EPOLLET | EPOLLIN);

        conns[i] = conn;
    }

    int handled_events = 0;
    struct epoll_event events[20];
    while (handled_events < n_events) {
        int n = epoll_wait(poll.registry.raw_fd, events, 10, -1);
        if (n < 1) {
            printf("epoll wait errror: %s\n", strerror(errno));
            exit(1);
        }

        for (int i = 0; i < n; i++) {
            size_t token = events[i].data.u64;
            char buf[4096];
            int nr;
            while (1) { // exusting edge trigger, we need to read until EAGAIN.
                nr = recv(conns[token].raw_fd, buf, sizeof(buf), 0);
                if (nr > 0) {
                    printf("RECEIVED: Event { %zu }\n", token);
                    printf("%.*s\n---------------------\n", nr, buf);
                    continue;
                }
                if (nr == 0)  { handled_events++; break; }
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;

                printf("{loop: %zu} recv error: %s\n", token, strerror(errno));
            }
        }
    }

    for (int i = 0; i < n_events; i++) {
        tcp_conn_cleanup(&conns[i]);
    }

    registery_cleanup(&poll.registry);
    return 0;
}
