#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 64
#define BUF_SIZE 1024
#define OUT_SIZE 1600

static int parse_port(const char *s) {
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || *end != '\0') return -1;
    if (val < 1 || val > 65535) return -1;
    return (int)val;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void addr_to_str(const struct sockaddr_in *a, char *ip_out, size_t ip_out_sz, int *port_out) {
    const char *s = inet_ntop(AF_INET, &a->sin_addr, ip_out, ip_out_sz);
    if (!s) strncpy(ip_out, "unknown", ip_out_sz);
    ip_out[ip_out_sz - 1] = '\0';
    *port_out = ntohs(a->sin_port);
}

static int find_free_slot(int fds[]) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (fds[i] == -1) return i;
    }
    return -1;
}

static void drop_client(int idx, int fds[], struct sockaddr_in addrs[]) {
    if (fds[idx] != -1) close(fds[idx]);
    fds[idx] = -1;
    memset(&addrs[idx], 0, sizeof(addrs[idx]));
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = parse_port(argv[1]);
    if (port < 0) {
        fprintf(stderr, "bad port: %s\n", argv[1]);
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    srv.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    int client_fds[MAX_CLIENTS];
    struct sockaddr_in client_addrs[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
        memset(&client_addrs[i], 0, sizeof(client_addrs[i]));
    }

    printf("server listening on port %d\n", port);
    printf("connect with: nc 127.0.0.1 %d\n", port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(listen_fd, &readfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] != -1) {
                FD_SET(client_fds[i], &readfds);
                if (client_fds[i] > maxfd) maxfd = client_fds[i];
            }
        }

        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);

            if (cfd < 0) {
                if (errno != EINTR) perror("accept");
            } else {
                int slot = find_free_slot(client_fds);
                if (slot == -1) {
                    const char *msg = "server full\n";
                    send_all(cfd, msg, strlen(msg));
                    close(cfd);
                    printf("rejected client, server full\n");
                } else {
                    client_fds[slot] = cfd;
                    client_addrs[slot] = caddr;

                    char ip[INET_ADDRSTRLEN];
                    int cport = 0;
                    addr_to_str(&caddr, ip, sizeof(ip), &cport);
                    printf("client connected %s:%d (slot %d)\n", ip, cport, slot);

                    const char *welcome = "connected. type messages and press enter\n";
                    send_all(cfd, welcome, strlen(welcome));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int cfd = client_fds[i];
            if (cfd == -1) continue;
            if (!FD_ISSET(cfd, &readfds)) continue;

            char buf[BUF_SIZE];
            ssize_t n = recv(cfd, buf, sizeof(buf), 0);

            if (n == 0) {
                char ip[INET_ADDRSTRLEN];
                int cport = 0;
                addr_to_str(&client_addrs[i], ip, sizeof(ip), &cport);
                printf("client disconnected %s:%d (slot %d)\n", ip, cport, i);
                drop_client(i, client_fds, client_addrs);
                continue;
            }

            if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                drop_client(i, client_fds, client_addrs);
                continue;
            }

            char ip[INET_ADDRSTRLEN];
            int cport = 0;
            addr_to_str(&client_addrs[i], ip, sizeof(ip), &cport);

            char out[OUT_SIZE];
            int out_len = snprintf(out, sizeof(out), "[%s:%d] %.*s", ip, cport, (int)n, buf);
            if (out_len < 0) continue;
            if (out_len > (int)sizeof(out)) out_len = (int)sizeof(out);

            printf("message from %s:%d (slot %d)\n", ip, cport, i);

            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (client_fds[j] == -1) continue;
                if (send_all(client_fds[j], out, (size_t)out_len) < 0) {
                    perror("send");
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) close(client_fds[i]);
    }
    close(listen_fd);
    return 0;
}
