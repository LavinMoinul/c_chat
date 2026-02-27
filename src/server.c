#include <arpa/inet.h>
#include <ctype.h>
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
#define INBUF_SIZE 1024
#define USERNAME_LEN 32
#define OUT_SIZE 1600

typedef struct {
    int fd;
    struct sockaddr_in addr;
    int has_name;
    char username[USERNAME_LEN];
    char inbuf[INBUF_SIZE];
    int inbuf_len;
} Client;

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

static void init_client(Client *c) {
    c->fd = -1;
    memset(&c->addr, 0, sizeof(c->addr));
    c->has_name = 0;
    c->username[0] = '\0';
    c->inbuf_len = 0;
    memset(c->inbuf, 0, sizeof(c->inbuf));
}

static int find_free_slot(Client clients[]) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) return i;
    }
    return -1;
}

static int find_newline(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') return i;
    }
    return -1;
}

static void remove_consumed(Client *c, int consumed) {
    int remaining = c->inbuf_len - consumed;
    if (remaining > 0) {
        memmove(c->inbuf, c->inbuf + consumed, (size_t)remaining);
    }
    c->inbuf_len = remaining;
}

static int is_valid_username(const char *name) {
    size_t len = strlen(name);
    if (len == 0 || len >= USERNAME_LEN) return 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

static void broadcast_all(Client clients[], const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) continue;
        if (send_all(clients[i].fd, msg, strlen(msg)) < 0) {
            perror("send");
        }
    }
}

static void drop_client(Client *c) {
    if (c->fd != -1) close(c->fd);
    init_client(c);
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

    Client clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        init_client(&clients[i]);
    }

    printf("server listening on port %d\n", port);
    printf("connect with: nc 127.0.0.1 %d\n", port);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(listen_fd, &readfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
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
                int slot = find_free_slot(clients);
                if (slot == -1) {
                    const char *msg = "server full\n";
                    send_all(cfd, msg, strlen(msg));
                    close(cfd);
                    printf("rejected client, server full\n");
                } else {
                    clients[slot].fd = cfd;
                    clients[slot].addr = caddr;

                    char ip[INET_ADDRSTRLEN];
                    int cport = 0;
                    addr_to_str(&caddr, ip, sizeof(ip), &cport);
                    printf("client connected %s:%d (slot %d)\n", ip, cport, slot);

                    const char *prompt = "enter username (letters, numbers, _ or -):\n";
                    send_all(cfd, prompt, strlen(prompt));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            Client *c = &clients[i];
            if (c->fd == -1) continue;
            if (!FD_ISSET(c->fd, &readfds)) continue;

            char recvbuf[512];
            ssize_t n = recv(c->fd, recvbuf, sizeof(recvbuf), 0);

            if (n == 0) {
                if (c->has_name) {
                    char leave_msg[OUT_SIZE];
                    snprintf(leave_msg, sizeof(leave_msg), "*** %s left ***\n", c->username);
                    broadcast_all(clients, leave_msg);
                    printf("client left: %s (slot %d)\n", c->username, i);
                } else {
                    printf("unnamed client disconnected (slot %d)\n", i);
                }
                drop_client(c);
                continue;
            }

            if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                drop_client(c);
                continue;
            }

            if ((size_t)n > (size_t)(INBUF_SIZE - c->inbuf_len)) {
                const char *msg = "input too long, disconnecting\n";
                send_all(c->fd, msg, strlen(msg));

                if (c->has_name) {
                    char leave_msg[OUT_SIZE];
                    snprintf(leave_msg, sizeof(leave_msg), "*** %s left ***\n", c->username);
                    broadcast_all(clients, leave_msg);
                    printf("dropped client for overflow: %s (slot %d)\n", c->username, i);
                } else {
                    printf("dropped unnamed client for overflow (slot %d)\n", i);
                }

                drop_client(c);
                continue;
            }

            memcpy(c->inbuf + c->inbuf_len, recvbuf, (size_t)n);
            c->inbuf_len += (int)n;

            while (1) {
                int newline_idx = find_newline(c->inbuf, c->inbuf_len);
                if (newline_idx == -1) break;

                char line[INBUF_SIZE];
                int line_len = newline_idx;

                if (line_len >= INBUF_SIZE) line_len = INBUF_SIZE - 1;
                memcpy(line, c->inbuf, (size_t)line_len);
                line[line_len] = '\0';

                if (line_len > 0 && line[line_len - 1] == '\r') {
                    line[line_len - 1] = '\0';
                }

                remove_consumed(c, newline_idx + 1);

                if (!c->has_name) {
                    if (!is_valid_username(line)) {
                        const char *msg = "invalid username. use letters, numbers, _ or -\n";
                        send_all(c->fd, msg, strlen(msg));
                        continue;
                    }

                    strncpy(c->username, line, sizeof(c->username) - 1);
                    c->username[sizeof(c->username) - 1] = '\0';
                    c->has_name = 1;

                    char join_msg[OUT_SIZE];
                    snprintf(join_msg, sizeof(join_msg), "*** %s joined ***\n", c->username);
                    broadcast_all(clients, join_msg);

                    printf("username set: %s (slot %d)\n", c->username, i);
                    continue;
                }

                if (line[0] == '\0') {
                    continue;
                }

                char out[OUT_SIZE];
                snprintf(out, sizeof(out), "%s: %s\n", c->username, line);

                printf("message from %s (slot %d): %s\n", c->username, i, line);
                broadcast_all(clients, out);
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1) close(clients[i].fd);
    }
    close(listen_fd);
    return 0;
}
