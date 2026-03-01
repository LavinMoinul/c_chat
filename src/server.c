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
#define OUT_SIZE 4096

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
    if (!s) {
        strncpy(ip_out, "unknown", ip_out_sz);
    }
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

static int username_exists(Client clients[], const char *name, Client *skip) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (&clients[i] == skip) continue;
        if (clients[i].fd == -1) continue;
        if (!clients[i].has_name) continue;
        if (strcmp(clients[i].username, name) == 0) return 1;
    }
    return 0;
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

static void send_user_list(Client clients[], Client *target) {
    char out[OUT_SIZE];
    int len = snprintf(out, sizeof(out), "Users:\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == -1) continue;

        const char *name = clients[i].has_name ? clients[i].username : "(registering)";
        int wrote = snprintf(out + len, sizeof(out) - (size_t)len, " - %s\n", name);
        if (wrote < 0) return;

        if ((size_t)wrote >= sizeof(out) - (size_t)len) {
            len = (int)sizeof(out) - 1;
            break;
        }
        len += wrote;
    }

    send_all(target->fd, out, strlen(out));
}

static int handle_command(Client clients[], Client *c, int slot, char *line) {
    if (strcmp(line, "/help") == 0) {
        const char *msg =
            "Commands:\n"
            " /help          Show commands\n"
            " /users         List connected users\n"
            " /nick NAME     Change username\n"
            " /quit          Disconnect\n";
        send_all(c->fd, msg, strlen(msg));
        return 0;
    }

    if (strcmp(line, "/users") == 0) {
        send_user_list(clients, c);
        return 0;
    }

    if (strcmp(line, "/quit") == 0) {
        send_all(c->fd, "Bye.\n", 5);

        if (c->has_name) {
            char leave_msg[OUT_SIZE];
            snprintf(leave_msg, sizeof(leave_msg), "*** %s left ***\n", c->username);
            broadcast_all(clients, leave_msg);
            printf("Client quit: %s (slot %d)\n", c->username, slot);
        }

        drop_client(c);
        return 1;
    }

    if (strcmp(line, "/nick") == 0 || strncmp(line, "/nick ", 6) == 0) {
        char *newname = line + 5;
        while (*newname == ' ') newname++;

        if (*newname == '\0') {
            const char *msg = "Usage: /nick NEW_NAME\n";
            send_all(c->fd, msg, strlen(msg));
            return 0;
        }

        if (!is_valid_username(newname)) {
            const char *msg = "Invalid username. Use letters, numbers, _ or - only.\n";
            send_all(c->fd, msg, strlen(msg));
            return 0;
        }

        if (username_exists(clients, newname, c)) {
            const char *msg = "That username is already taken.\n";
            send_all(c->fd, msg, strlen(msg));
            return 0;
        }

        char oldname[USERNAME_LEN];
        strncpy(oldname, c->username, sizeof(oldname) - 1);
        oldname[sizeof(oldname) - 1] = '\0';

        strncpy(c->username, newname, sizeof(c->username) - 1);
        c->username[sizeof(c->username) - 1] = '\0';

        char rename_msg[OUT_SIZE];
        snprintf(rename_msg, sizeof(rename_msg), "*** %s is now known as %s ***\n", oldname, c->username);
        broadcast_all(clients, rename_msg);
        printf("User renamed: %s -> %s (slot %d)\n", oldname, c->username, slot);
        return 0;
    }

    send_all(c->fd, "Unknown command. Try /help\n", 28);
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = parse_port(argv[1]);
    if (port < 0) {
        fprintf(stderr, "Bad port: %s\n", argv[1]);
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

    printf("Server listening on port %d\n", port);
    printf("Connect with: nc 127.0.0.1 %d\n", port);

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
                    const char *msg = "Server full\n";
                    send_all(cfd, msg, strlen(msg));
                    close(cfd);
                    printf("Rejected client, server full\n");
                } else {
                    clients[slot].fd = cfd;
                    clients[slot].addr = caddr;

                    char ip[INET_ADDRSTRLEN];
                    int cport = 0;
                    addr_to_str(&caddr, ip, sizeof(ip), &cport);
                    printf("Client connected %s:%d (slot %d)\n", ip, cport, slot);

                    const char *prompt = "Enter username (letters, numbers, _ or -):\n";
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
                    printf("Client left: %s (slot %d)\n", c->username, i);
                } else {
                    printf("Unnamed client disconnected (slot %d)\n", i);
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
                send_all(c->fd, "Input too long, disconnecting\n", 29);

                if (c->has_name) {
                    char leave_msg[OUT_SIZE];
                    snprintf(leave_msg, sizeof(leave_msg), "*** %s left ***\n", c->username);
                    broadcast_all(clients, leave_msg);
                    printf("Dropped client for overflow: %s (slot %d)\n", c->username, i);
                } else {
                    printf("Dropped unnamed client for overflow (slot %d)\n", i);
                }

                drop_client(c);
                continue;
            }

            memcpy(c->inbuf + c->inbuf_len, recvbuf, (size_t)n);
            c->inbuf_len += (int)n;

            int dropped = 0;

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
                        const char *msg = "Invalid username. Use letters, numbers, _ or - only.\n";
                        send_all(c->fd, msg, strlen(msg));
                        continue;
                    }

                    if (username_exists(clients, line, c)) {
                        const char *msg = "That username is already taken.\n";
                        send_all(c->fd, msg, strlen(msg));
                        continue;
                    }

                    strncpy(c->username, line, sizeof(c->username) - 1);
                    c->username[sizeof(c->username) - 1] = '\0';
                    c->has_name = 1;

                    char join_msg[OUT_SIZE];
                    snprintf(join_msg, sizeof(join_msg), "*** %s joined ***\n", c->username);
                    broadcast_all(clients, join_msg);

                    send_all(c->fd, "Type /help for commands.\n", 25);
                    printf("Username set: %s (slot %d)\n", c->username, i);
                    continue;
                }

                if (line[0] == '\0') continue;

                if (line[0] == '/') {
                    if (handle_command(clients, c, i, line)) {
                        dropped = 1;
                        break;
                    }
                    continue;
                }

                char out[OUT_SIZE];
                snprintf(out, sizeof(out), "%s: %s\n", c->username, line);
                printf("Message from %s (slot %d): %s\n", c->username, i, line);
                broadcast_all(clients, out);
            }

            if (dropped) continue;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1) close(clients[i].fd);
    }
    close(listen_fd);
    return 0;
}
