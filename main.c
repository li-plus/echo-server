#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int is_server_running = 1;

int set_blocking(int fd, int is_blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl()");
        return -1;
    }
    flags = is_blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (fcntl(fd, F_SETFL, flags) != 0) {
        perror("fcntl()");
        return -1;
    }
    return 0;
}

int handle_event_read(int active_fd) {
    uint8_t buffer[BUFSIZ];
    ssize_t len = recv(active_fd, buffer, BUFSIZ - 1, 0);
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            perror("recv()");
            return -1;
        }
    } else if (len == 0) {
        return -1;
    } else {
        buffer[len] = '\0';
        printf("[%2d]: %s", active_fd, (char *) buffer);
        send(active_fd, buffer, len, 0);
        return 0;
    }
}

void server_loop(int server_fd) {
    int epfd = epoll_create1(0);

    struct epoll_event socket_events[SOMAXCONN];

    socket_events[server_fd].events = EPOLLIN;
    socket_events[server_fd].data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &socket_events[server_fd]);

    struct epoll_event events[SOMAXCONN];

    while (is_server_running) {
        int num_events = epoll_wait(epfd, events, SOMAXCONN, -1);

        // Process all read events first
        for (int i = 0; i < num_events; i++) {
            if (events[i].events & EPOLLIN) {
                int active_fd = events->data.fd;
                if (active_fd == server_fd) {
                    // Accept incoming connection
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len = sizeof(client_addr);

                    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
                    if (client_fd < 0) {
                        perror("accept()");
                        continue;
                    }
                    // Reject if connection pool is full
                    if (client_fd >= SOMAXCONN) {
                        fprintf(stderr, "Rejected client from %s:%d due to too many connections\n",
                                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                        close(client_fd);
                        continue;
                    }
                    // Disable Nagle algorithm to forward packets ASAP
                    int optval = 1;
                    if (setsockopt(client_fd, SOL_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0) {
                        perror("setsockopt()");
                        close(client_fd);
                        continue;
                    }
                    // Set socket to non-block
                    if (set_blocking(client_fd, 0) != 0) {
                        close(client_fd);
                        continue;
                    }
                    // Add socket to epoll events
                    socket_events[client_fd].events = EPOLLIN;
                    socket_events[client_fd].data.fd = client_fd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &socket_events[client_fd]) != 0) {
                        perror("epoll_ctl()");
                        close(client_fd);
                        continue;
                    }

                    printf("Accepted connection from %s:%d with FD %d\n",
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);
                } else {
                    // Client echo, send it back
                    if (handle_event_read(active_fd) == -1) {
                        printf("Closing FD %d\n", active_fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, active_fd, &socket_events[active_fd]);
                        close(active_fd);
                    }
                }
            }
        }
    }
    close(epfd);
}

void print_help(const char *prog_name) {
    printf("USAGE: %s [-h] [-p PORT]\n", prog_name);
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        is_server_running = 0;
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, &handle_signal);

    int bind_port = 9999;

    int ch;
    while ((ch = getopt(argc, argv, "p:h")) != -1) {
        switch (ch) {
            case 'p':
                bind_port = atoi(optarg);
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Create a socket using TCP protocol over IPv4
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }
    // Reuse address
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt()");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (set_blocking(server_fd, 0) != 0) {
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Bind socket to given address
    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(bind_port);
    if (bind(server_fd, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind()");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    // Listen to socket
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen()");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on %s:%d\n", inet_ntoa(bind_addr.sin_addr), bind_port);
    // Run server
    server_loop(server_fd);
    close(server_fd);
    printf("Server exited\n");
    return 0;
}
